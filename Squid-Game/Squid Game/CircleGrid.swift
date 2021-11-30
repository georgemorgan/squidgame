//
//  Honeycomb.swift
//  Squid Game
//
//  Created by SquidGame on 10/26/21.
//

import SwiftUI

struct CircleGrid: View {
    var totalWidth: CGFloat
    var players: [Player]
    var hiddenPlayerIDs: Set<Player.ID>
    var namespace: Namespace.ID
    var onTapPlayer: (Player) -> Void

    var numPlayersPerRow: Int {
        let players = Int(round(totalWidth / (imgSize.width * 2)))
        if players <= 1 {
            return 3
        }
        return players
    }

    var spacing: CGFloat {
        4
    }

    var numHexagonsPerRow: Int {
        numPlayersPerRow * 2
    }

    var chunks: [(Int, ArraySlice<Player>)] {
        Array(players.chunks(ofSize: 100).enumerated())
    }

    var columns: [GridItem] {
        let count = totalWidth < 700 ? 5 : 10
        return [GridItem](repeating: GridItem(.flexible(minimum: 50, maximum: 85), spacing: 6), count: count)
    }

    var body: some View {
        LazyVGrid(columns: columns) {
            ForEach(chunks, id: \.0) { (row, players) in
                    ForEach(players) { player in
                        if hiddenPlayerIDs.contains(player.number) {
                            Circle()
                                .aspectRatio(1, contentMode: .fit)
                                .hidden()
                        } else {
                            Button {
                                onTapPlayer(player)
                            } label: {
                                PlayerCell(player: player) {
                                    Circle()
                                }
                                .aspectRatio(1, contentMode: .fit)
                            }
                            .buttonStyle(.plain)
                        }
                    }
            }
        }
    }
}

