//
//  Honeycomb.swift
//  Squid Game
//
//  Created by SquidGame on 10/26/21.
//

import SwiftUI

struct Honeycomb: View {
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
        Array(players.chunks(ofSize: numPlayersPerRow).enumerated())
    }

    var body: some View {
        LazyVStack(alignment: .center, spacing:  -(hexagonWidth * 0.68) + spacing) {
            ForEach(chunks, id: \.0) { (row, players) in
                HStack(spacing: 0) {
                    if !row.isMultiple(of: 2) {
                        Spacer()
                            .frame(width: (hexagonWidth * 0.88) * 2 + spacing)
                    }
                    ForEach(players) { player in
                        if hiddenPlayerIDs.contains(player.number) {
                            PlayerHexagon()
                                .frame(width: imgSize.width, height: imgSize.height)
                                .hidden()
                        } else {
                            Button {
                                onTapPlayer(player)
                            } label: {
                                PlayerCell(player: player) {
                                    PolygonShape(sides: 6)
                                }
                                .frame(width: imgSize.width, height: imgSize.height)
                            }
                            .buttonStyle(.plain)
                        }
                        Spacer()
                            .frame(width: (hexagonWidth * 0.65) + spacing)
                    }
                    if players.count < numPlayersPerRow {
                        ForEach(0..<numPlayersPerRow - players.count, id: \.self) { _ in
                                PlayerHexagon()
                                    .frame(width: imgSize.width, height: imgSize.height)
                                    .hidden()
                            Spacer()
                                .frame(width: (hexagonWidth * 0.65) + spacing)
                        }
                    }
                }
            }
        }
    }
}

