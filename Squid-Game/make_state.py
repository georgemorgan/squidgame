import sys
import json
from webserver import Player, default_players, DumpEncoder

initial_state = default_players(457)
for id, player in initial_state.items():
    player.is_alive = False

with open(sys.argv[1]) as alive_ids:
    for line in alive_ids.readlines():
        initial_state[int(line)].is_alive = True

print(json.dumps(initial_state, indent=2, sort_keys=True, cls=DumpEncoder))
