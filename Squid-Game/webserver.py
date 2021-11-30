import argparse
import asyncio
import json
import websockets
import os
import threading
import time
from transmit import Board, log
from json import JSONEncoder
import argparse

class DumpEncoder(JSONEncoder):
    def default(self, o):
        return o.__dict__

class Player(object):
    def __init__(self, is_alive: bool, number: int):
        self.number = number
        self.is_alive = is_alive
        self.image_url = ""

def default_players(count: int):
    return {
        n: Player(
            is_alive=True,
            number=n
        ) for n in range(1, count + 1)
    }

class PlayerController(object):
    def __init__(self, filename: str, default_player_count: int, is_revive_allowed: bool=False):
        self.is_revive_allowed = is_revive_allowed
        if os.path.exists(filename):
            try:
                with open(filename) as existing_file:
                    raw_players = json.load(existing_file)
                    self.players = {}
                    for n, player_json in raw_players.items():
                        new_player = Player(
                            is_alive=player_json["is_alive"],
                            number=player_json["number"],
                        )
                        self.players[int(n)] = new_player
                    log(f'found existing file with {len(self.players)} players')
            except BaseException as err:
                log(f'existing JSON file was malformed ({err}), moving to {filename}.malformed')
                self.players = default_players(default_player_count)
        else:
            self.players = default_players(default_player_count)

        self._file = open(filename, 'w+')
        self.write_state_to_file()

    def write_state_to_file(self):
        self._file.seek(0)
        json.dump(self.players, self._file, cls=DumpEncoder, indent=2, sort_keys=True)
        self._file.truncate()

    def set_player_liveness(self, number, is_alive):
        if is_alive and not self.is_revive_allowed:
            log(f'ignoring request to revive {number}')
            return

        player = self.players[number]
        if player:
            player.is_alive = is_alive
            log(f'player {number} has been {"revived" if is_alive else "eliminated"}')
        else:
            log(f'error: unknown player {number}')

    def generate_initial_data_event(self):
        return json.dumps({
            "type": "initial_data",
            "players": self.players
        }, cls=DumpEncoder, indent=2, sort_keys=True)

    def generate_update_event(self):
        return json.dumps({
            "type": "update",
            "alive": { str(p.number): (1 if p.is_alive else 0) for p in self.players.values() }
        })

    def dead_player_ids(self):
        ids = []
        for id, player in self.players.items():
            if not player.is_alive:
                ids.append(id)
        return ids


    def __del__(self):
        self._file.close()

class Server(object):
    def __init__(self, player_controller, board, disable_kills):
        self.player_controller = player_controller
        self.connected_clients = set()
        self.board = board
        self.disable_kills = disable_kills

    async def run(self, websocket, path):
        try:
            # Register user
            self.connected_clients.add(websocket)
            log(f"Added client (count: {len(self.connected_clients)}): {websocket.remote_address}")
            # Send the set of initial data to the user
            initial_data = self.player_controller.generate_initial_data_event()
            await websocket.send(initial_data)
            # Manage state changes
            async for message in websocket:
                data = json.loads(message)
                action = data["action"]
                if action == "eliminate" or action == "revive":
                    numbers_to_toggle = data["numbers"]
                    for number_to_toggle in numbers_to_toggle:
                        is_alive = action == "revive"
                        self.player_controller.set_player_liveness(number_to_toggle, is_alive)
                    self.player_controller.write_state_to_file()
                    dead_players = self.player_controller.dead_player_ids()
                    if not self.disable_kills:
                        self.board.kill(dead_players)
                    update_data = self.player_controller.generate_update_event()
                    websockets.broadcast(self.connected_clients, update_data)
                elif action == "arm":
                    log('devices armed')
                    self.board.arm(True)
                elif action == "disarm":
                    log('devices disarmed')
                    self.board.arm(False)
                else:
                    log("error: unsupported event: %s", data)
        except websockets.ConnectionClosedError:
            pass
        finally:
            # Unregister user
            self.connected_clients.remove(websocket)
            log(f'Removed client (count: {len(self.connected_clients)}) {websocket.remote_address}')


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('device', help='The file on disk where the device is mounted')
    parser.add_argument('--players', type=int, help='The number of players playing, defaults to 456', default=456)
    parser.add_argument('--allow-revive', action='store_true', help='Whether to allow reviving players. Defaults to False.', default=False)
    parser.add_argument('--disable-kills', action='store_true', help='Whether to send detonation reqeusts to boards. Defaults to False.', default=False)
    args = parser.parse_args()
    board = Board(args.device)
    player_controller = PlayerController('state.json', default_player_count=args.players, is_revive_allowed=args.allow_revive)

    def read_loop():
        while True:
            line = board.serial.read_until()
            log(f'<<< {line.decode("utf-8", "ignore")}', end='')
            time.sleep(0.1)

    def send_detonation_loop():
        while True:
            time.sleep(1)
            dead_players = player_controller.dead_player_ids()
            if not args.disable_kills:
                board.kill(dead_players)

    read_thread = threading.Thread(target=read_loop)
    read_thread.start()

    detonation_update_loop = threading.Thread(target=send_detonation_loop)
    detonation_update_loop.start()

    server = Server(player_controller, board, args.disable_kills)
    async with websockets.serve(server.run, "0.0.0.0", 8765):
        await asyncio.Future()  # run forever


if __name__ == "__main__":
    asyncio.run(main())
