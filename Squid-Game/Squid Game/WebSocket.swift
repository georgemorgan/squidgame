//
//  File.swift
//  
//
//  Created by SquidGame on 3/21/21.
//

import Foundation

private enum MessageType: String, Codable {
    case initial_data
    case update
}

enum ServerMessage {
    /// The initial set of players and their data
    case initialData([Player])

    /// A mapping of all player IDs to whether they are alive or not.
    case update([Int: Bool])

    /// An error from the server
    case error(Error)

    /// An explicit disconnection from the server
    case disconnected
}

enum ServerCommand {
    case arm
    case disarm
    case eliminate([Int])
    case revive([Int])
}

actor WebSocket: NSObject, URLSessionWebSocketDelegate {
    private var newMessageSink: AsyncStream<ServerMessage>.Continuation?
    private var socket: URLSessionWebSocketTask?
    var pingTask: Task<Void, Error>?
    var receiveTask: Task<Void, Error>?
    private var id = 0
    let encoder = JSONEncoder()
    let decoder = JSONDecoder()
    var session: URLSession!

    var request: URLRequest {
        var components = URLComponents()
        components.scheme = "ws"
        components.host = UserDefaults.standard.string(forKey: "serverHost")
        components.port = UserDefaults.standard.integer(forKey: "serverPort")
        var request = URLRequest(url: components.url!)
        request.addValue("application/json", forHTTPHeaderField: "Content-Type")
        return request
    }

    func connect() async -> AsyncStream<ServerMessage> {
        self.socket = self.session.webSocketTask(with: self.request)
        self.socket?.resume()

        return AsyncStream { continuation in
            newMessageSink = continuation
        }
    }

    override init() {
        super.init()
        self.session = URLSession(configuration: .default, delegate: self, delegateQueue: .main)
    }

    func beginPinging() {
        pingTask = Task {
            while !Task.isCancelled {
                do {
                    try await socket?.sendPing()
                } catch {
                    newMessageSink?.yield(.error(error))
                }
                try await Task.sleep(nanoseconds: 2 * NSEC_PER_SEC)
            }
        }
    }

    func send(_ command: ServerCommand) async throws {
        struct PlayerActionMessage: Codable {
            var action = "eliminate"
            var numbers: [Int]
        }
        let data: Data
        switch command {
        case .arm:
            data = try encoder.encode(["action": "arm"])
        case .disarm:
            data = try encoder.encode(["action": "disarm"])
        case .eliminate(let ids):
            data = try encoder.encode(PlayerActionMessage(action: "eliminate", numbers: ids))
        case .revive(let ids):
            data = try encoder.encode(PlayerActionMessage(action: "revive", numbers: ids))
        }
        try await socket?.send(.data(data))
    }

    func handleMessage(_ data: Data) throws {
        struct MessageTypeWrapper: Codable {
            var type: MessageType
        }
        let type = try decoder.decode(MessageTypeWrapper.self, from: data)
        switch type.type {
        case .initial_data:
            struct PlayerWrapper: Codable {
                var players: [Int: Player]
            }
            print(String(decoding: data, as: UTF8.self))
            let initialData = try decoder.decode(PlayerWrapper.self, from: data)
            let players: [Player] =
                initialData.players.values.sorted {
                    $0.number < $1.number
                }
            newMessageSink?.yield(.initialData(players))
        case .update:
            struct AliveMapWrapper: Codable {
                var alive: [String: Int]
            }
            let update = try decoder.decode(AliveMapWrapper.self, from: data)
            var intIDs = [Int: Bool]()
            for (id, alive) in update.alive {
                guard let intID = Int(id) else {
                    continue
                }
                intIDs[intID] = alive != 0
            }
            newMessageSink?.yield(.update(intIDs))
        }
    }

    func disconnect() {
        socket?.cancel()
        receiveTask?.cancel()
        receiveTask = nil
        pingTask?.cancel()
        pingTask = nil
        newMessageSink?.finish()
        newMessageSink = nil
    }

    private func listenAsync() async throws {
        guard let socket = socket else { return }
        while !Task.isCancelled {
            let result = try await socket.receive()
            switch result {
            case .data(let data):
                do {
                    try self.handleMessage(data)
                } catch {
                    print("failed to handle message: \(error)")
                }
            case .string(let string):
                do {
                    try self.handleMessage(Data(string.utf8))
                } catch {
                    print("failed to handle message: \(error)")
                }
            @unknown default:
                print("unknown message result: \(result)")
            }
        }
    }

    func listen() {
        receiveTask = Task {
            try await listenAsync()
        }
    }

    deinit {
        socket?.cancel()
    }

    nonisolated func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        if let error = error {
            Task {
                await newMessageSink?.yield(.error(error))
            }
        } else {
            Task {
                await newMessageSink?.yield(.disconnected)
            }
        }
    }

    nonisolated func urlSession(_ session: URLSession, webSocketTask: URLSessionWebSocketTask, didOpenWithProtocol protocol: String?) {
        print("Connected!")
        Task {
            await beginPinging()
            await listen()
        }
    }

    nonisolated func urlSession(_ session: URLSession, webSocketTask: URLSessionWebSocketTask, didCloseWith closeCode: URLSessionWebSocketTask.CloseCode, reason: Data?) {
        print("Closed! \(closeCode)")
    }
}

extension URLSessionWebSocketTask {
    func sendPing() async throws {
        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
            sendPing { error in
                if let error = error {
                    continuation.resume(throwing: error)
                } else {
                    continuation.resume()
                }
            }
        }
    }
}
