#!/usr/bin/env python3
"""Real executable, private Unix listener; supplied wire policy, no display/device."""
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
binary = root / 'bemenu-sophia'
env = dict(os.environ)
for key in ('SOPHIA_SHELL_SOCKET', 'DISPLAY', 'WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'BEMENU_BACKEND', 'BEMENU_RENDERER'):
    env.pop(key, None)

def frame(kind, tx, payload):
    return struct.pack('<4sHHQII', b'SOPH', 1, kind, tx, len(payload), 0) + payload

def exact(peer, count):
    data = b''
    while len(data) < count:
        part = peer.recv(count-len(data))
        assert part, 'unexpected executable disconnect'
        data += part
    return data

def receive(peer):
    magic, version, kind, tx, length, reserved = struct.unpack('<4sHHQII', exact(peer, 24))
    assert magic == b'SOPH' and version == 1 and not reserved and length <= 65536
    return kind, tx, exact(peer, length)

assert subprocess.run([binary, '--help'], env=env, capture_output=True, timeout=2).returncode == 0
assert subprocess.run([binary, '--serve'], env=env, capture_output=True, timeout=2).returncode == 1
assert subprocess.run([binary, '--unknown'], env=env, capture_output=True, timeout=2).returncode == 2
limits = next(bytes.fromhex(line.split()[1]) for line in
    (root/'vendor/sophia-shell/sophia-shell-content.frames').read_text().splitlines()
    if line.startswith('content-161 '))

for mode in ('reopen', 'wrong_revision', 'startup_timeout'):
    with tempfile.TemporaryDirectory(prefix='bemenu-native-') as directory:
        path = str(Path(directory)/'peer.sock')
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
            listener.bind(path)
            listener.listen(1)
            listener.settimeout(2)
            child = subprocess.Popen([binary, '--serve'], env=dict(env, SOPHIA_SHELL_SOCKET=path),
                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                with listener.accept()[0] as peer:
                    peer.settimeout(2)
                    kind, tx, hello = receive(peer)
                    assert kind == 96 and tx == 0 and struct.unpack('<HHQ', hello) == (7, 7, 0x9a0)
                    if mode == 'startup_timeout':
                        stdout, stderr = child.communicate(timeout=7)
                        assert child.returncode == 1 and b'timeout=1' in stderr
                    else:
                        welcome = struct.pack('<HHQQHHHH', 6 if mode == 'wrong_revision' else 7, 0, 11, 0x9a0, 16, 128, 16, 0)
                        peer.sendall(frame(97, 0, welcome))
                        if mode == 'wrong_revision':
                            stdout, stderr = child.communicate(timeout=2)
                            assert child.returncode == 1 and b'stage=service' in stderr
                        else:
                            peer.sendall(limits + frame(114, 9, struct.pack('<QQHH', 11, 9, 0, 0)) +
                                frame(116, 9, struct.pack('<QQ', 11, 9)))
                            facts = struct.pack('<QQQIIQQIIIIQ', 11, 3, 1, 1, 0, 2, 7, 1280, 720, 1, 1, 1)
                            peer.sendall(frame(162, 70, facts))
                            for opening in (1, 2):
                                peer.sendall(frame(187, 50+opening, struct.pack('<7Q', 11, 3, opening, 2, 7, 9, 1)))
                                kind, tx, request = receive(peer)
                                assert kind == 188 and struct.unpack_from('<Q', request, 16)[0] == opening
                                request_id = struct.unpack_from('<Q', request, 40)[0]
                                assert request_id == opening
                                refusal = bytearray(160)
                                struct.pack_into('<QQQHH', refusal, 0, 11, 3, request_id, 2, 2)
                                struct.pack_into('<QQ', refusal, 32, 2, 7)
                                peer.sendall(frame(164, tx, refusal) +
                                    frame(197, 90+opening, struct.pack('<QQQHH', 11, 3, opening, 11, 0)))
                            child.terminate()
                            stdout, stderr = child.communicate(timeout=2)
                            assert child.returncode == 0 and stderr.count(b'status=negotiated') == 1
                    assert not stdout
            finally:
                if child.poll() is None:
                    child.kill()
                    child.communicate(timeout=2)
print('bemenu_executable private_socket=pass openings=2 startup_timeout=pass native=false')
