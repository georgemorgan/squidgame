//
//  SettingsView.swift
//  Squid Game
//
//  Created by SquidGame on 10/26/21.
//

import SwiftUI

struct SettingsView: View {
    @State var serverHost: String = ""
    @State var serverPort: String = ""
    @ObservedObject var controller: PlayerController
    @Environment(\.dismiss) var dismiss

    var body: some View {
        NavigationView {
            Form {
                Section("Server Info") {
                    HStack {
                        TextField("Server Host", text: $serverHost)
                        Spacer()
                        Text("Server Host")
                    }
                    HStack {
                        TextField("Server Port", text: $serverPort)
                            .keyboardType(.numberPad)
                        Spacer()
                        Text("Server Port")
                    }
                    Button("Submit") {
                        UserDefaults.standard.set(serverHost, forKey: "serverHost")
                        UserDefaults.standard.set(serverPort, forKey: "serverPort")

                        Task {
                            await controller.disconnect()
                            await controller.connect()
                        }
                    }
                }
                .onAppear {
                    serverHost = UserDefaults.standard.string(forKey: "serverHost") ?? ""
                    serverPort = UserDefaults.standard.string(forKey: "serverPort") ?? ""
                }

                Section("Device Control") {
                    Button("Arm Devices") {
                        controller.arm()
                    }

                    Button("Disarm Devices") {
                        controller.disarm()
                    }
                }
            }
            .navigationTitle(Text("Settings"))
            .toolbar {
                Button("Done") {
                    dismiss()
                }
            }
        }
    }
}
