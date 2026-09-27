"""Independent 9P fixture for the executable test; supplied Session decisions.

Field layouts follow the pinned C SDK's shell_files codecs. The SDK's literal
Limits vector supplies the valid base; the fixture raises only named limits.
No IPC framing is used, and no native presentation is claimed.
"""
from pathlib import Path
import re
import struct

EPOCH = 11
CONTENT = 3


def unpack(fmt, value, at=0):
    return struct.unpack_from('<' + fmt, value, at)


def pack(fmt, *values):
    return struct.pack('<' + fmt, *values)


def exact(peer, size):
    data = b''
    while len(data) < size:
        part = peer.recv(size - len(data))
        if not part:
            raise EOFError('executable disconnected')
        data += part
    return data


def record(kind, body, sequence=0):
    return pack('IHHQQQ', 32 + len(body), 1, kind, EPOCH, 0, sequence) + body


def limits(root):
    vectors = (Path(root) / 'vendor/sophia-desktop-sdk/source/src/tests/shell_files_vectors.h').read_text()
    body = re.search(r'vector_Limits\[\] = \{([^}]+)\}', vectors)[1]
    value = bytearray(bytes(int(v, 16) for v in re.findall(r'0x([0-9a-f]{2})', body))[32:])
    # Body offsets from shell_files/limits.c at SDK a0ab8c85.
    for at, n in {0: EPOCH, 8: CONTENT, 24: 1048576, 32: 1048576,
                  40: 1048576, 48: 1048576, 56: 3145728}.items():
        struct.pack_into('<Q', value, at, n)
    for at, n in {80: 8192, 84: 8144, 88: 1024, 92: 1024, 144: 1,
                  180: 8216, 188: 16, 196: 1024, 204: 50,
                  220: 1000, 224: 2000, 228: 500, 232: 1000,
                  236: 1000, 240: 2000, 244: 1000, 248: 250,
                  252: 2000}.items():
        struct.pack_into('<I', value, at, n)
    return record(1, value)


class Peer:
    def __init__(self, socket, root, wrong_revision=False):
        self.socket = socket
        self.wrong_revision = wrong_revision
        self.fids = {}
        self.qids = {name: i + 1 for i, name in enumerate(
            ('', 'api', 'transaction', 'submit', 'ack', 'events', 'limits', 'catalog', 'outputs'))}
        self.objects = {
            'api': b'sophia-shell-files version=1 role=launcher epoch=11 fd_transfer=none\n',
            'limits': limits(root),
            'catalog': record(3, pack('QQQHHI', 9, EPOCH, 9, 0, 0, 0)),
            'outputs': record(2, pack('QQQQIIQQIIIIQ', 70, EPOCH, CONTENT, 3, 1, 0,
                                      2, 7, 1280, 720, 1, 1, 1)),
        }
        self.staged = bytearray()
        self.journal = bytearray()
        self.reader = None
        self.sequence = self.accepted = self.acked = self.submission = 0
        self.allocations = 0
        self.finished = False

    def qid(self, name):
        return pack('BIQ', 128 if not name else 0, 0, self.qids[name])

    def send(self, kind, tag, body=b''):
        self.socket.sendall(pack('IBH', 7 + len(body), kind, tag) + body)

    def event(self, kind, body):
        self.sequence += 1
        self.journal.extend(record(kind, body, self.sequence))

    def publish(self, kind, generation, name):
        self.event(19, pack('H6xQQ', kind, generation, self.qids[name]))

    def opening(self, opening):
        self.event(38, pack('8Q', 50 + opening, EPOCH, CONTENT, opening, 2, 7, 9, 1))

    def events(self):
        if self.reader is not None:
            tag, offset, count = self.reader
            data = self.journal[offset:offset + count]
            if data:
                self.reader = None
                self.send(117, tag, pack('I', len(data)) + data)

    def submit(self, value):
        epoch, submission, length, reserved = unpack('QQII', value)
        size, version, kind, rec_epoch, rec_id, sequence = unpack('IHHQQQ', self.staged)
        assert epoch == rec_epoch == EPOCH and submission == rec_id > self.submission
        assert not reserved and not sequence and version == 1 and size == length == len(self.staged)
        self.submission = submission
        self.event(18, pack('QH6x', submission, kind))
        self.accepted = self.sequence
        body = self.staged[32:]
        if kind == 256:
            assert unpack('HH4xQ', body) == (7, 7, 0x9a0)
            self.event(16, pack('HHQQHHHHI', 6 if self.wrong_revision else 7, 0,
                                EPOCH, 0x9a0, 16, 128, 16, 1, 0))
            if not self.wrong_revision:
                self.publish(3, 9, 'catalog')
                self.publish(2, 3, 'outputs')
                self.opening(1)
        else:
            assert kind == 266, f'unexpected submitted kind {kind}'
            tx, epoch, content, opening, output, generation, request = unpack('7Q', body)
            self.allocations += 1
            assert (epoch, content, opening, output, generation, request) == (
                EPOCH, CONTENT, self.allocations, 2, 7, self.allocations)
            assert unpack('HHII', body, 72) == (1, 1, 640, 320)
            refusal = bytearray(168)
            struct.pack_into('<QQQQHH', refusal, 0, tx, EPOCH, CONTENT, request, 2, 2)
            struct.pack_into('<QQ', refusal, 40, 2, 7)
            self.event(32, refusal)
            self.event(43, pack('4QH2x', 90 + opening, EPOCH, CONTENT, opening, 11))
            if self.allocations == 1:
                self.opening(2)

    def step(self):
        size, kind, tag = unpack('IBH', exact(self.socket, 7))
        assert 7 <= size <= 8192
        body = exact(self.socket, size - 7)
        if kind == 100:
            assert body[6:] == b'9P2000.L'
            self.send(101, tag, pack('IH', 8192, 8) + b'9P2000.L')
        elif kind == 104:
            self.fids[unpack('I', body)[0]] = ''
            self.send(105, tag, self.qid(''))
        elif kind == 110:
            fid, newfid, count = unpack('IIH', body)
            if count:
                assert count == 1
                length, = unpack('H', body, 10)
                name = body[12:12 + length].decode()
            else:
                name = self.fids[fid]
            assert name in self.qids
            self.fids[newfid] = name
            self.send(111, tag, pack('H', count) + self.qid(name) * count)
        elif kind == 12:
            name = self.fids[unpack('I', body)[0]]
            if name == 'transaction':
                assert self.acked >= self.accepted, 'transaction opened before custody ack'
                self.staged.clear()
            self.send(13, tag, self.qid(name) + pack('I', 0))
        elif kind == 120:
            del self.fids[unpack('I', body)[0]]
            self.send(121, tag)
        elif kind == 116:
            fid, offset, count = unpack('IQI', body)
            name = self.fids[fid]
            if name == 'events':
                assert self.reader is None and offset <= len(self.journal)
                self.reader = (tag, offset, count)
            else:
                # Small reads exercise object-fetch continuation through EOF.
                value = self.objects[name][offset:offset + min(count, 79)]
                self.send(117, tag, pack('I', len(value)) + value)
        elif kind == 118:
            fid, offset, count = unpack('IQI', body)
            value = body[16:]
            assert len(value) == count
            name = self.fids[fid]
            if name == 'transaction':
                assert offset == len(self.staged)
                self.staged.extend(value)
            elif name == 'submit':
                assert offset == 0 and count == 24
                self.submit(value)
            else:
                assert name == 'ack' and count == 16
                epoch, self.acked = unpack('QQ', value)
                assert epoch == EPOCH and self.acked <= self.sequence
                if self.allocations == 2 and self.acked == self.sequence:
                    self.finished = True
            self.send(119, tag, pack('I', count))
        else:
            raise AssertionError(f'unexpected 9P request {kind}')
        self.events()
