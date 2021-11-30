//
//  MovieLoader.swift
//  MovieDB
//
//  Created by SquidGame on 7/9/21.
//

import Foundation
import UIKit

enum ImageError: Error {
    case unableToDecode(Data)
}

/// Describes the potential states of a resource being loaded from the network.
enum LoadState<Value> {
    /// The loading process has not started yet.
    case idle

    /// The load is currently in progress.
    case loading

    /// The value has successfully loaded
    case loaded(Value)

    /// There was an error while loading
    case failed(Error)
}

/// MovieLoader is a view model that performs network requests and updates state that is observed by Views.
@MainActor
final class ImageCache: ObservableObject {
    @Published var images: [URL: LoadState<UIImage>] = [:]

    /// Loads and caches the image at the provided URL. This may be called multiple times,
    /// and subsequent calls will not perform any side effects.
    func loadImage(at url: URL) -> LoadState<UIImage> {
        if let state = images[url] {
            return state
        }

        images[url] = .loading

        Task {
            do {
                let (data, _) = try await URLSession.shared.data(from: url)
                guard let image = UIImage(data: data) else {
                    throw ImageError.unableToDecode(data)
                }
                images[url] = .loaded(image)
            } catch {
                images[url] = .failed(error)
            }
        }

        return .loading
    }
}
