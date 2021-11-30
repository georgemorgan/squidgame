//
//  Squid_GameApp.swift
//  Squid Game
//
//  Created by SquidGame on 10/22/21.
//

import SwiftUI

@main
struct Squid_GameApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
                .onAppear {
                    for family in UIFont.familyNames {
                        print("Family name " + family)
                        let fontNames = UIFont.fontNames(forFamilyName: family)

                        for font in fontNames {
                            print("    Font name: " + font)
                        }
                    }
                    UINavigationBar.appearance().titleTextAttributes = [
                        .font : UIFont(name: "GameOfSquids", size: 12)!
                    ]
                    UINavigationBar.appearance().largeTitleTextAttributes = [
                        .font : UIFont(name: "GameOfSquids", size: 36)!
                    ]
                    UserDefaults.standard.register(defaults: [
                        "serverHost": "squidnet.local",
                        "serverPort": "8765"
                    ])
                }
        }
    }
}
