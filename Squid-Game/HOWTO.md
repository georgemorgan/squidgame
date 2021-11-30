## How to:

### Start the Server

`python3 webserver.py /dev/ttyUSB0`

Pass `--disable-kills` to prevent it from transmitting detonations.
Pass `--allow-revive` (for testing) to allow the apps to revive people.

Open the app
  - Open Settings, tap Arm Devices (VERY IMPORTANT)

### Reset the State

The State is stored in `state.json`, in this format:

```json
{
  "1": {
    "is_alive": true,
    "number": 1,
    "image_url": ""
  }
}
```

The `image_url` field is unused, but present for backwards compatibility with
old app versions.

To reset the state entirely back to fresh, delete `state.json` -- the server
will re-create a new one.

Every time a player is eliminated or revived, the current state is re-written
to disk entirely.

There's a file, `make_state.py`, that will generate a `state.json` from a file
that has a single number per line, each representing a currently-alive player.

### Reprogram the board

William can flash a .bin that George has sent him onto the boards.

### Reset an ID

`python3 transmit.py set-board-id <id>`

Or you can manually enter serial into `picocom` after connecting with

```
picocom -l /dev/ttyUSB0 --lower-dtr --lower-rts -b 115200
```

### Serial Protocol (Receiver)

#### Set Board ID

`#SID,000;` (must be zero-padded)

#### Read Board ID

`#RID,;` (yes, the comma is intentional, it's a bug we couldn't fix)

### Serial Protocol (Transmitter)

#### Detonate Field

The DET command is the most complicated.

```
#DET,<128 hexadecimal digits>;
```

This corresponds to 512 bits, each bit representing an active board.
Each byte shifts left by the `identifier % 8`, and is placed into
the byte corresponding to the `identifier / 8`.

So a board containing 1, 4, and 7, would have the first byte as

```
10010010
^  ^  ^
|  |  |
|  |  --- byte 1 is set
|  |------byte 4 is set
|---------byte 7 is set
```


