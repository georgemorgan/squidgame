//
//  ContentView.swift
//  Squid Game
//
//  Created by SquidGame on 10/22/21.
//

import SwiftUI

extension String {
    func leftPad(to length: Int, with char: Character) -> String {
        let count = count
        if count < length {
            return String(repeating: char, count: length - count) + self
        } else {
            return self
        }
    }
}

struct ExpandedPlayerView: View {
    var player: Player
    var namespace: Namespace.ID
    @Binding var displayedPlayer: Player?
    @ObservedObject var controller: PlayerController

    var body: some View {
        ZStack {
            Color.black.opacity(0.6)
                .ignoresSafeArea()
                .transition(.opacity)
                .onTapGesture {
                    withAnimation(.funSpring) {
                        displayedPlayer = nil
                    }
                }
            VStack {
                PlayerCell(player: player) {
                    PolygonShape(sides: 6)
                }
                .transition(.scale.combined(with: .opacity))
                Button {
                    withAnimation(.funSpring) {
                        controller.togglePlayerAliveness([player])
                        displayedPlayer = nil
                    }
                } label: {
                    if player.isAlive {
                        Text("Eliminate Player \(player.number)")
                        Spacer()
                        Image(systemName: "person.fill.xmark")
                    } else {
                        Text("Revive Player \(player.number)")
                        Spacer()
                        Image(systemName: "person.fill.checkmark")
                    }
                }
                .transition(.opacity.combined(with: .scale(scale: 0.8, anchor: .top).animation(.funSpring)))
            }
            .buttonStyle(player.isAlive ? .playerButton : .reviveButton)
            .frame(maxWidth: 400)
            .padding(40)
        }
    }
}

struct MultipleSelectionView: View {
    @Binding var players: [Player]
    var totalWidth: CGFloat
    var controller: PlayerController
    var namespace: Namespace.ID
    var onTapPlayer: (Player) -> Void

    var eliminateText: String {
        let action = (players.first?.isAlive ?? true) ? "Eliminate" : "Revive"
        if players.count == 1 {
            return "\(action) Player \(players[0].number)"
        } else {
            return "\(action) \(players.count) Players"
        }
    }

    var body: some View {
        VStack {
            CircleGrid(
                totalWidth: totalWidth,
                players: players,
                hiddenPlayerIDs: [],
                namespace: namespace,
                onTapPlayer: onTapPlayer
            )
            Button {
                withAnimation(.funSpring) {
                    controller.togglePlayerAliveness(players)
                    players = []
                }
            } label: {
                HStack {
                    Text(eliminateText)
                    Spacer()
                    if players.first?.isAlive == true {
                        Image(systemName: "person.fill.xmark")
                    } else {
                        Image(systemName: "person.fill.checkmark")
                    }
                }
            }
            .buttonStyle(players.first?.isAlive == true ? .playerButton : .reviveButton)
            Button {
                withAnimation(.funSpring) {
                    players = []
                }
            } label: {
                Text("Cancel")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.playerButton)
        }
        .padding()
        .background {
            Color(uiColor: .systemBackground)
                .overlay(alignment: .top) {
                    Rectangle()
                        .fill(.secondary)
                        .frame(height: 1)
                }
                .ignoresSafeArea()
                .allowsHitTesting(false)
        }
    }
}

struct ContentView: View {
    @State var searchString = ""
    @State var displayedPlayer: Player?
    @State var isShowingSettings: Bool = false
    @State var hideEliminated = false
    @State var selectedPlayers = [Player]()
    @FocusState var isEditingTextField: Bool
    @Namespace var namespace
    @StateObject var imageCache = ImageCache()
    @StateObject var controller = PlayerController()
    @Environment(\.horizontalSizeClass) var horizontalSizeClass

    var hiddenPlayers: Set<Player.ID> {
        var hidden = Set(selectedPlayers.map(\.number))
        if let player = displayedPlayer {
            hidden.insert(player.number)
        }
        return hidden
    }

    var mainDisplayedPlayers: [Player] {
        var fullList = hideEliminated ? controller.alivePlayers : controller.players
        if !searchString.isEmpty {
            fullList.removeAll { String($0.number).leftPad(to: 3, with: "0").range(of: searchString) == nil }
        }
        return fullList
    }

    func clearTextField() {
        if !searchString.isEmpty {
            searchString = ""
        }
    }

    func select(_ player: Player) {
        clearTextField()

        // Don't select the same player twice
        if selectedPlayers.contains(where: { $0.number == player.number }) {
            return
        }

        // All players must either be eliminated or not, no mixing and matching
        if let first = selectedPlayers.first, first.isAlive != player.isAlive {
            return
        }
        selectedPlayers.append(player)
    }

    var honeycomb: some View {
        GeometryReader { proxy in
            ScrollView {
                if mainDisplayedPlayers.count == 1, let player = mainDisplayedPlayers.first {
                    Button {
                        clearTextField()
                        controller.togglePlayerAliveness([player])
                    } label: {
                        if player.isAlive {
                            Text("Eliminate Player \(player.number)")
                            Image(systemName: "person.fill.xmark")
                        } else {
                            Text("Revive Player \(player.number)")
                            Image(systemName: "person.fill.checkmark")
                        }
                    }
                    .font(.body)
                    .padding(.horizontal)
                    .buttonStyle(player.isAlive ? .playerButton : .reviveButton)
                } else if let player = mainDisplayedPlayers.first(where: {
                    $0.number == Int(searchString)
                }) {
                    Button {
                        clearTextField()
                        controller.togglePlayerAliveness([player])
                    } label: {
                        if player.isAlive {
                            Text("Eliminate Player \(player.number)")
                            Image(systemName: "person.fill.xmark")
                        } else {
                            Text("Revive Player \(player.number)")
                            Image(systemName: "person.fill.checkmark")
                        }
                    }
                    .font(.body)
                    .padding(.horizontal)
                    .buttonStyle(player.isAlive ? .playerButton : .reviveButton)
                    .padding(.vertical)
                }
                CircleGrid(
                    totalWidth: proxy.size.width,
                    players: mainDisplayedPlayers,
                    hiddenPlayerIDs: hiddenPlayers,
                    namespace: namespace,
                    onTapPlayer: { player in
                        withAnimation(.funSpring) {
                            select(player)
                        }
                    }
                )
            }
            .padding(.horizontal)
            .safeAreaInset(edge: .bottom) {
                if !selectedPlayers.isEmpty {
                    MultipleSelectionView(
                        players: $selectedPlayers,
                        totalWidth: proxy.size.width,
                        controller: controller,
                        namespace: namespace
                    ) { player in
                        withAnimation(.funSpring) {
                            selectedPlayers.removeAll {
                                $0.number == player.number
                            }
                        }
                    }
                }
            }
        }
        .buttonStyle(.playerButton)
    }

    @MainActor
    @ViewBuilder
    var mainContent: some View {
        switch controller.connectionState {
        case .disconnected:
            VStack {
                ProgressView()
                Button("Reconnect") {
                    Task {
                        await controller.disconnect()
                        await controller.connect()
                    }
                }
            }
        case .error(let error):
            VStack {
                Text("Disconnected from Server")
                    .font(.title.bold())
                Text(error.localizedDescription)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                Button("Reconnect") {
                    Task {
                        await controller.disconnect()
                        await controller.connect()
                    }
                }
            }
            .buttonStyle(.playerButton)
        case .connected:
            if controller.players.isEmpty {
                Button("Reconnect") {
                    Task {
                        await controller.disconnect()
                        await controller.connect()
                    }
                }
                .buttonStyle(.playerButton)
            }
            VStack {
                HStack {
                    HStack {
                    Image(systemName: "magnifyingglass")
                        .foregroundColor(.secondary)
                    TextField("Search", text: $searchString)
                        .focused($isEditingTextField)
                        .overlay(alignment: .trailing) {
                            if !searchString.isEmpty {
                                Button {
                                    searchString = ""
                                } label: {
                                    Image(systemName: "x.circle.fill")
                                        .foregroundColor(.secondary)
                                        .font(.body)
                                        .opacity(0.8)
                                }
                                .padding(.horizontal)
                            }
                        }
                    }
                    .font(.system(size: horizontalSizeClass == .regular ? 60 : 40))
                    .padding(6)
                    .background(Color.secondary.opacity(0.2), in: RoundedRectangle(cornerRadius: 9))
                    if isEditingTextField {
                        Button("Cancel") {
                            clearTextField()
                            isEditingTextField = false
                        }
                        .buttonStyle(.playerButton)
                    }
                }
                .padding(.horizontal)
                .keyboardType(.numberPad)

                honeycomb
            }
        }
    }

    @MainActor
    var body: some View {
        ZStack {
            NavigationView {
                mainContent
                    .toolbar {
                        ToolbarItemGroup(placement: .navigationBarLeading) {
                            Toggle("Hide Eliminated", isOn: $hideEliminated.animation(.funSpring))
                        }

                        ToolbarItemGroup(placement: .navigationBarTrailing) {
                            Text("\(controller.players.count(where: \.isAlive))")
                                .foregroundColor(.white)
                                .font(.headline)
                                .padding(.horizontal)
                                .padding(.vertical, 2)
                                .background(Capsule().foregroundColor(.darkGreen))
                                .fixedSize(horizontal: true, vertical: false)
                            Text("\(controller.players.count - controller.players.count(where: \.isAlive))")
                                .foregroundColor(.white)
                                .font(.headline)
                                .padding(.horizontal)
                                .padding(.vertical, 2)
                                .background(Capsule().foregroundColor(.pink))
                                .padding(.horizontal)
                                .fixedSize(horizontal: true, vertical: false)

                            Button {
                                isShowingSettings = true
                            } label: {
                                Image(systemName: "gear")
                            }
                        }
                    }
                    .navigationTitle("Squid Game")
            }
            .navigationViewStyle(.stack)
            if let player = displayedPlayer {
                ExpandedPlayerView(
                    player: player,
                    namespace: namespace,
                    displayedPlayer: $displayedPlayer,
                    controller: controller)
            }
        }
        .sheet(isPresented: $isShowingSettings) {
            SettingsView(controller: controller)
        }
        .onChange(of: isShowingSettings) { [isShowingSettings] newValue in
            print("isShowingSettings: \(isShowingSettings) -> \(newValue)")
        }
        .environmentObject(imageCache)
        .task {
            await controller.connect()
        }
    }
}

extension Animation {
    static var funSpring: Animation {
        .interpolatingSpring(mass: 2, stiffness: 300, damping: 35)
    }
}

struct PlayerButtonStyle: ButtonStyle {
    var tint: Color = .pink
    func makeBody(configuration: Configuration) -> some View {
        HStack {
            configuration.label
        }
        .padding()
        .foregroundColor(.white)
        .background(tint, in: Capsule())
        .font(.body.bold())
        .scaleEffect(configuration.isPressed ? 0.9 : 1)
        .animation(.interactiveSpring(), value: configuration.isPressed)
    }
}

extension ButtonStyle where Self == PlayerButtonStyle {
    static var playerButton: PlayerButtonStyle {
        PlayerButtonStyle()
    }
    static var reviveButton: PlayerButtonStyle {
        PlayerButtonStyle(tint: .green)
    }
}

struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
    }
}

extension Array {
    func count(where isTrue: (Element) -> Bool) -> Int {
        var c = 0
        for x in self {
            if isTrue(x) {
                c += 1
            }
        }
        return c
    }
}

extension Color {
    static var darkGreen: Color {
        Color(red: 48/255, green: 133/255, blue: 20/255)
    }
}
