import serial
import argparse
import bitstring
import time
import datetime

def log(msg, **kwargs):
    time_str = datetime.datetime.now().strftime("%I:%M:%S %p")
    print(f'[{time_str}] {msg}', **kwargs)

def chunks(lst, n):
    """Yield successive n-sized chunks from lst."""
    for i in range(0, len(lst), n):
        yield lst[i:i + n]

class Board(object):
    def __init__(self, device):
        self.serial = serial.serial_for_url(device, 115200, do_not_open=True)
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.open()

    def write_str(self, string):
        log(f">>> {string}")
        self.serial.write(string.encode('utf-8'))

    def set_id(self, number):
        padded_num = str(number).zfill(3)
        self.write_str(f'#SID,{padded_num};')

    def read_id(self):
        self.write_str(f'#RID,;')
        self.serial.read_until('\n')

    def kill(self, ids):
        indices = (id for id in ids)
        bits = bitstring.BitArray('0x' + ('0' * 128))
        bits.set(1, indices)

        final_bit_str = "".join(byte_str[::-1] for byte_str in chunks(bits.bin, 8))
        final_bits = bitstring.BitArray(f'0b{final_bit_str}')
        self.write_str(f'#DET,{final_bits.hex};')

    def arm(self, armed):
        self.write_str(f'#ARM,{1 if armed else 0};')

    def reset(self):
        self.serial.dtr = False
        self.serial.dtr = True


if __name__ == '__main__':
    def set_board_id(args):
        board = Board(args.device)
        time.sleep(1)
        board.set_id(args.number)

    def read_board_id(args):
        board = Board(args.device)
        time.sleep(1)
        board.read_id()

    def kill(args):
        board = Board(args.device)
        time.sleep(1)
        board.kill(args.ids)

    def arm(args):
        board = Board(args.device)
        time.sleep(1)
        board.arm(True)

    def disarm(args):
        board = Board(args.device)
        time.sleep(1)
        board.arm(False)

    def reset(args):
        board = Board(args.device)
        board.reset()
        time.sleep(1)

    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers()

    parser.add_argument('--device', type=str, help='The location of the USB device the board is mounted to (/dev/ttyXXX)')

    set_board_id_command = subparsers.add_parser('set-board-id')
    set_board_id_command.add_argument('number', type=int)
    set_board_id_command.set_defaults(func=set_board_id)

    read_board_id_command = subparsers.add_parser('read-board-id')
    read_board_id_command.set_defaults(func=read_board_id)

    kill_command = subparsers.add_parser('kill')
    kill_command.add_argument('ids', type=int, nargs='+')
    kill_command.set_defaults(func=kill)

    arm_command = subparsers.add_parser('arm')
    arm_command.set_defaults(func=arm)

    disarm_command = subparsers.add_parser('disarm')
    disarm_command.set_defaults(func=disarm)

    reset_command = subparsers.add_parser('reset')
    reset_command.set_defaults(func=reset)

    args = parser.parse_args()
    args.func(args)
