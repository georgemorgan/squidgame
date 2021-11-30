//
//  PlayerCell.swift
//  Squid Game
//
//  Created by SquidGame on 10/26/21.
//

import SwiftUI

var imgSize: CGSize {
    CGSize(width: 75, height: 75)
}

var hexagonWidth: CGFloat {
    (imgSize.width / 2) * cos(.pi / 6) * 2
}

struct PolygonShape: Shape {
    var sides: Int

    func path(in rect: CGRect) -> Path {
        let h = min(rect.size.width, rect.size.height) / 2.0
        let c = CGPoint(x: rect.size.width / 2.0, y: rect.size.height / 2.0)
        var path = Path()

        for i in 0..<sides {
            let angle = (Double(i) * (360.0 / Double(sides))) * Double.pi / 180

            let pt = CGPoint(x: c.x + CGFloat(cos(angle)) * h, y: c.y + sin(angle) * h)

            if i == 0 {
                path.move(to: pt) // move to first vertex
            } else {
                path.addLine(to: pt) // draw line to next vertex
            }
        }

        path.closeSubpath()

        return path
    }
}

struct PlayerHexagon: View {
    var body: some View {
        Rectangle()
            .fill(.background)
            .clipShape(PolygonShape(sides: 6))
            .contentShape(PolygonShape(sides: 6))
    }
}

struct PlayerImage: View {
    var imageURL: URL
    @EnvironmentObject var imageCache: ImageCache

    var body: some View {
        switch imageCache.loadImage(at: imageURL) {
        case .loaded(let image):
            Image(uiImage: image)
                .resizable()
                .scaledToFill()
                .saturation(0.7)
                .overlay {
                    Color.pink
                        .blendMode(.hue)
                }
                .overlay {
                    LinearGradient(
                        colors: [
                            .clear,
                            .black.opacity(0.7)
                        ],
                        startPoint: UnitPoint(x: 0.5, y: 0.3),
                        endPoint: .bottom
                    )
                }
        case .loading:
            Color.pink
        default:
            Color.pink
        }
    }
}

struct PlayerCell<Background: Shape>: View {
    var player: Player
    @ViewBuilder var backgroundShape: Background

    var body: some View {
        backgroundShape
            .fill(Color.pink)
            .opacity(player.isAlive ? 1 : 0.4)
            .clipShape(backgroundShape)
            .overlay {
                GeometryReader { proxy in
                    Text(verbatim: "\(player.number)".leftPad(to: 3, with: "0"))
                        .font(.custom("DS-Digital-Bold", size: proxy.size.width * 0.5))
                        .opacity(1)
                        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .center)
                }
            }
            .foregroundColor(.white)
            .aspectRatio(1, contentMode: .fit)
            .drawingGroup()
    }
}

struct PlayerCellPreviews: PreviewProvider {
    static var previews: some View {
        HStack {
            PlayerCell(
                player: Player(
                    number: 3,
                    is_alive: false
                )
            ) {
                PolygonShape(sides: 6)
            }
            PlayerCell(
                player: Player(
                    number: 193,
                    is_alive: true
                )
            ) {
                PolygonShape(sides: 6)
            }
        }
        .frame(height: 75)
        .environmentObject(ImageCache())
    }
}
