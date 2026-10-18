"""Independent 9P fixture for the executable test; supplied Session decisions.

Field layouts follow the pinned C SDK's shell_files codecs. The SDK's literal
Limits vector supplies the valid base; the fixture raises only named limits.
No IPC framing is used, and no native presentation is claimed.
"""
from pathlib import Path
import re
import struct
import select
import socket as sockets
import array
import fcntl
import os

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
    def __init__(self, socket, root, wrong_revision=False, upload=False):
        self.socket = socket
        self.wrong_revision = wrong_revision
        self.upload = upload
        self.image_ready = self.source_received = False
        self.image_metadata = None
        self.fids = {}
        self.qids = {name: i + 1 for i, name in enumerate(
            ('', 'api', 'transaction', 'transaction-reuse', 'submit', 'ack', 'events', 'limits', 'catalog', 'outputs'))}
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
        self.image_listener = sockets.socket(sockets.AF_UNIX, sockets.SOCK_SEQPACKET)
        self.image_path = str(Path(socket.getsockname()).with_suffix('.image'))
        self.image_listener.bind(self.image_path)
        self.image_listener.listen(1)
        self.image = None
        endpoint = Path(self.image_path).name
        self.objects['image-channel'] = (
            'sophia-image-channel version=1\nrole=1\nconnection_epoch=11\nchannel_generation=1\n'
            f'endpoint={endpoint}\nsource_kinds=1\nmax_packet_bytes=200\nmax_fds=4\nmax_offers=1\n'
            'max_packets_per_turn=8\nhello_timeout_ms=500\nvalidation_timeout_ms=100\n'
            'validation_chunk_bytes=65536\ncapture_interest_timeout_ms=200\nformats_generation=1\n'
            'limits_generation=1\n').encode()
        self.objects['image-limits'] = record(0x7005, pack(
            '6Q12IQ', 1, 1, 1048576, 1048576, 0, 0,
            0, 1, 1, 0, 1, 1, 1024, 1024, 100, 200, 65536, 8, 0))
        self.objects['image-formats'] = record(0x7006, pack('3QII', 1, 1, 1, 0, 0))
        for name in ('image-channel', 'image-limits', 'image-formats'):
            self.qids[name] = len(self.qids) + 1

    def close(self):
        if self.image is not None:
            self.image.close()
        self.image_listener.close()
        Path(self.image_path).unlink(missing_ok=True)

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
            assert unpack('HH4xQ', body) == (9, 9, 0x29a0)
            self.event(16, pack('HHQQHHHHI', 7 if self.wrong_revision else 9, 0,
                                EPOCH, 0x29a0, 16, 128, 16, 1, 0))
            if not self.wrong_revision:
                self.publish(3, 9, 'catalog')
                self.publish(2, 3, 'outputs')
                self.opening(1)
        elif kind == 266:
            tx, epoch, content, opening, output, generation, request = unpack('7Q', body)
            self.allocations += 1
            assert (epoch, content, opening, output, generation, request) == (
                EPOCH, CONTENT, self.allocations, 2, 7, self.allocations)
            assert unpack('HHII', body, 72) == (1, 1, 640, 320)
            refusal = bytearray(168)
            struct.pack_into('<QQQQHH', refusal, 0, tx, EPOCH, CONTENT, request, 2, 2)
            struct.pack_into('<QQ', refusal, 40, 2, 7)
            if self.upload:
                struct.pack_into('<HH', refusal, 32, 1, 0)
                struct.pack_into('<QQ', refusal, 56, 21, 1)
                struct.pack_into('<Q', refusal, 88, 1)
                struct.pack_into('<8I', refusal, 96, 320, 200, 640, 320, 320, 200, 640, 320)
                struct.pack_into('<II', refusal, 128, 1, 1)
            self.event(32, refusal)
            if self.upload:
                return
            self.event(43, pack('4QH2x', 90 + opening, EPOCH, CONTENT, opening, 11))
            if self.allocations == 1:
                self.opening(2)
        elif kind == 0x7000:
            assert self.upload and self.image_ready and len(body) == 96
            assert unpack('7Q', body)[3:] == (CONTENT, 2, 21, 3)
            assert unpack('5IHHQQ', body, 56) == (1, 1, 640, 320, 2560, 1, 1, 819200, 0)
            self.image_metadata = bytes(body)
            self.event(0x7001, body)
        elif kind == 263:
            assert self.upload and self.source_received
            self.finished = True
        else:
            raise AssertionError(f'unexpected submitted kind {kind}')

    def step(self):
        ready, _, _ = select.select([self.socket, self.image or self.image_listener], [], [], 10)
        assert ready, 'image/9P fixture deadline'
        if self.image_listener in ready:
            self.image, _ = self.image_listener.accept()
            return
        if self.image is not None and self.image in ready:
            packet, ancillary, flags, _ = self.image.recvmsg(201, sockets.CMSG_SPACE(16))
            assert not flags
            if not self.image_ready:
                assert not ancillary and packet == pack('IHHHHI3Q', 40, 1, 1, 1, 0, 0, EPOCH, 1, 0)
                self.image_ready = True
                self.event(0x7003, pack('QII', 1, 1, 0))
            else:
                assert self.upload and self.image_metadata is not None
                offer, resource, generation = unpack('3Q', self.image_metadata)
                assert len(packet) == 200 and packet[40:136] == self.image_metadata
                assert packet[:40] == pack('IHHHHI3Q', 200, 1, 2, 1, 1, 0, EPOCH, 1, offer)
                assert len(ancillary) == 1 and ancillary[0][:2] == (sockets.SOL_SOCKET, sockets.SCM_RIGHTS)
                fds = array.array('i', ancillary[0][2])
                assert len(fds) == 1
                try:
                    assert fcntl.fcntl(fds[0], fcntl.F_GET_SEALS) & 15 == 15
                    assert os.fstat(fds[0]).st_size == 819200
                    pixels = os.pread(fds[0], 819200, 0)
                    assert len(set(pixels)) > 1
                    assert all(max(pixels[i:i+3]) <= pixels[i+3] for i in range(0, len(pixels), 4))
                finally:
                    os.close(fds[0])
                self.source_received = True
                self.event(33, pack('5QHHIQ', offer, EPOCH, CONTENT, resource, generation, 2, 0, 0, 819200))
                self.event(0x7002, pack('3QII', offer, resource, generation, 1, 0))
            self.events()
            return
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
            assert name in self.qids, name
            self.fids[newfid] = name
            self.send(111, tag, pack('H', count) + self.qid(name) * count)
        elif kind == 12:
            name = self.fids[unpack('I', body)[0]]
            if name in ('transaction', 'transaction-reuse'):
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
            if name in ('transaction', 'transaction-reuse'):
                if name == 'transaction-reuse' and offset == 0:
                    self.staged.clear()
                assert offset == len(self.staged)
                self.staged.extend(value)
            elif name == 'submit':
                assert offset == 0 and count == 24
                self.submit(value)
            else:
                assert name == 'ack' and count == 16
                epoch, self.acked = unpack('QQ', value)
                assert epoch == EPOCH and self.acked <= self.sequence
                if not self.upload and self.image_ready and self.allocations == 2 and self.acked == self.sequence:
                    self.finished = True
            self.send(119, tag, pack('I', count))
        else:
            raise AssertionError(f'unexpected 9P request {kind}')
        self.events()
