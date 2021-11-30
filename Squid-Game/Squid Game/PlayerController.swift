//
//  PlayerController.swift
//  Squid Game
//
//  Created by SquidGame on 10/26/21.
//

import Foundation
import SwiftUI

struct Player: Identifiable, Equatable, Comparable {
    var number: Int
    var is_alive: Bool

    var isAlive: Bool {
        get { is_alive }
        set { is_alive = newValue }
    }

    var id: Int { number }

    static func <(lhs: Self, rhs: Self) -> Bool {
        lhs.number < rhs.number
    }
}

extension Player: Codable {}

extension Player {
    static func defaultPlayers() -> [Player] {
        return (0..<456).map {
            Player(number: $0 + 1, is_alive: true)
        }
    }
}

@MainActor
final class PlayerController: ObservableObject {
    enum ConnectionState {
        case disconnected
        case connected(Task<Void, Never>)
        case error(Error)
    }
    var players = [Player]()
    var socket: WebSocket
    @Published var connectionState: ConnectionState = .disconnected

    var isConnected: Bool {
        if case .connected = connectionState { return true }
        return false
    }

    init() {
        socket = WebSocket()
    }

    var alivePlayers: [Player] {
        players.filter(\.isAlive)
    }

    var eliminatedPlayers: [Player] {
        players.filter { !$0.isAlive }
    }

    func togglePlayerAliveness(_ players: [Player]) {
        var toEliminate = [Int]()
        var toRevive = [Int]()
        for player in players {
            if player.isAlive {
                toEliminate.append(player.number)
            } else {
                toRevive.append(player.number)
            }
            if let idx = self.players.firstIndex(where: { $0.number == player.number }) {
                self.players[idx].isAlive.toggle()
            }
        }

        let elim = toEliminate
        let revive = toRevive
        Task {
            if !elim.isEmpty {
                try await socket.send(.eliminate(elim))
            }

            if !revive.isEmpty {
                try await socket.send(.revive(revive))
            }
        }
    }

    func arm() {
        Task {
            try await socket.send(.arm)
        }
    }

    func disarm() {
        Task {
            try await socket.send(.disarm)
        }
    }

    func disconnect() async {
        await socket.disconnect()
        connectionState = .disconnected
        objectWillChange.send()
    }

    func readMessages(from stream: AsyncStream<ServerMessage>) async throws {
        for try await message in stream {
            switch message {
            case .initialData(let players):
                self.players = players
                objectWillChange.send()
            case .update(let newAliveMap):
                for idx in players.indices {
                    let number = players[idx].number
                    guard let aliveness = newAliveMap[number] else {
                        continue
                    }
                    players[idx].isAlive = aliveness
                }
                withAnimation(.funSpring) {
                    objectWillChange.send()
                }
            case .error(let error):
                connectionState = .error(error)
            case .disconnected:
                connectionState = .disconnected
            }
        }
    }

    func connect() async {
        players = []
        let messages = await socket.connect()
        let task = Task {
            do {
                try await readMessages(from: messages)
            } catch {
                connectionState = .error(error)
            }
        }
        connectionState = .connected(task)
        objectWillChange.send()
    }
}
