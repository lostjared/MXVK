"""Nonblocking TCP snapshots compatible with the native Tetris wire format."""

import errno
import select
import socket
import time

from .game import HEIGHT, WIDTH

PORT = 37373


def snapshot(game, sequence=0):
    cells = ''.join('.' if color < 0 else str(color) for row in game.board for color in row)
    values = ['S', sequence, game.score, game.lines, game.level, int(game.game_over), cells, game.color, game.x, game.y]
    values.extend(value for cell in game.cells for value in cell)
    return ' '.join(map(str, values)) + '\n'


def parse_snapshot(line):
    fields = line.split()
    if len(fields) != 18 or fields[0] != 'S' or len(fields[6]) != WIDTH * HEIGHT or any(c not in '.01234567' for c in fields[6]):
        raise ValueError('invalid Tetris snapshot')
    numbers = [int(value) for value in fields[1:6] + fields[7:]]
    if not 0 <= numbers[5] <= 7 or any(abs(value) > 1000000000 for value in numbers):
        raise ValueError('invalid snapshot values')
    return {'score': numbers[1], 'lines': numbers[2], 'level': numbers[3], 'over': bool(numbers[4]), 'board': [(-1 if c == '.' else int(c)) for c in fields[6]], 'color': numbers[5], 'x': numbers[6], 'y': numbers[7], 'cells': tuple(zip(numbers[8::2], numbers[9::2]))}


class Network:
    def __init__(self):
        self.listener = self.peer = None
        self.connecting = False
        self.connected = False
        self.status = 'H to host, J/Enter to join'
        self.opponent = None
        self.received = b''
        self.pending = b''
        self.sequence = 0
        self.last_send = 0.0

    def close(self):
        for sock in (self.peer, self.listener):
            if sock is not None:
                sock.close()
        self.peer = self.listener = None
        self.connected = self.connecting = False
        self.received = self.pending = b''
        self.opponent = None

    def host(self):
        self.close()
        try:
            self.listener = socket.socket()
            self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.listener.bind(('', PORT))
            self.listener.listen(1)
            self.listener.setblocking(False)
            self.status = f'Waiting for a peer on port {PORT}'
        except OSError as error:
            self.close()
            self.status = str(error)

    def join(self, address):
        self.close()
        try:
            socket.inet_aton(address)
            self.peer = socket.socket()
            self.peer.setblocking(False)
            result = self.peer.connect_ex((address, PORT))
            if result not in (0, errno.EINPROGRESS, errno.EWOULDBLOCK, errno.EALREADY):
                raise OSError(result, 'Connection failed')
            self.connecting = True
            self.connect_started = time.monotonic()
            self.status = 'Connecting...'
        except OSError as error:
            self.close()
            self.status = str(error)

    def update(self, game):
        was_connected = self.connected
        try:
            if self.listener is not None:
                try:
                    self.peer, _ = self.listener.accept()
                except BlockingIOError:
                    return False
                self.listener.close()
                self.listener = None
                self.peer.setblocking(False)
                self.connected = True
            if self.connecting:
                _, writable, failed = select.select([], [self.peer], [self.peer], 0)
                if failed or time.monotonic() - self.connect_started > 10:
                    raise OSError('Connection timed out')
                if not writable:
                    return False
                error = self.peer.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
                if error:
                    raise OSError(error, 'Connection failed')
                self.connecting = False
                self.connected = True
            if not self.connected:
                return False
            if not was_connected:
                self.status = 'Connected'
                # Let the application reset its board before the first snapshot.
                return True
            now = time.monotonic()
            if not self.pending and now - self.last_send >= 0.05:
                self.pending = snapshot(game, self.sequence).encode('ascii')
                self.sequence += 1
                self.last_send = now
            if self.pending:
                try:
                    sent = self.peer.send(self.pending)
                    if sent == 0:
                        raise OSError('Peer disconnected')
                    self.pending = self.pending[sent:]
                except BlockingIOError:
                    pass
            for _ in range(16):
                try:
                    data = self.peer.recv(4096)
                except BlockingIOError:
                    break
                if not data:
                    raise OSError('Peer disconnected')
                self.received += data
                while b'\n' in self.received:
                    line, self.received = self.received.split(b'\n', 1)
                    try:
                        self.opponent = parse_snapshot(line.decode('ascii'))
                    except (ValueError, UnicodeError):
                        pass
                if len(self.received) > 8192:
                    raise OSError('Invalid peer data')
        except OSError as error:
            self.close()
            self.status = str(error)
        return False
