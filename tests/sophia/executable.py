#!/usr/bin/env python3
"""Real executable over private 9P; supplied outcomes, no display/device proof."""
import os
from pathlib import Path
import socket
import shutil
import subprocess
import tempfile
import time
from file_peer import Peer, exact, unpack

root = Path(__file__).resolve().parents[2]
# The selected executable works without sibling libbemenu or plugin files.
# Cairo/Pango remain system libraries; this is not a fully static binary.
with tempfile.TemporaryDirectory(prefix='bemenu-executable-') as directory:
    binary = Path(directory) / 'bemenu-sophia'
    shutil.copy2(root / 'bemenu-sophia', binary)
    env = dict(os.environ)
    for key in ('SOPHIA_SHELL_SOCKET', 'SOPHIA_SHELL_9P_SOCKET', 'DISPLAY',
                'WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'BEMENU_BACKEND', 'BEMENU_RENDERER',
                'SOPHIA_LAUNCH_AUTH_FD'):
        env.pop(key, None)
    symbols = subprocess.run(['nm', '--defined-only', binary], check=True,
                             capture_output=True, text=True, timeout=5).stdout
    assert not any(line.split()[-1].startswith(('sophia_shell_', 'sophia_wm_'))
                   for line in symbols.splitlines() if line.split()), 'IPC code linked'
    assert 'sophia_ns_next' in symbols and 'sophia_ss_dispatch' in symbols
    assert subprocess.run([binary, '--help'], env=env, capture_output=True, timeout=2).returncode == 0
    assert subprocess.run([binary, '--serve'], env=env, capture_output=True, timeout=2).returncode == 1
    assert subprocess.run([binary, '--unknown'], env=env, capture_output=True, timeout=2).returncode == 2
    endpoints = [{'SOPHIA_SHELL_9P_SOCKET': ''}]
    for retired in ('', '/retired'):
        endpoints.append({'SOPHIA_SHELL_SOCKET': retired})
        endpoints.append({'SOPHIA_SHELL_SOCKET': retired, 'SOPHIA_SHELL_9P_SOCKET': '/files'})
    for selection in endpoints:
        refused = subprocess.run([binary, '--serve'], env=dict(env, **selection),
                                 capture_output=True, timeout=2)
        assert refused.returncode == 1 and b'stage=connect' in refused.stderr

    auth_path = str(Path(directory) / 'auth.sock')
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
        listener.bind(auth_path)
        listener.listen(1)
        listener.setblocking(False)
        for value in ('', '0', '2', '-1', '+3', '3x', '2147483648', '2147483647'):
            refused = subprocess.run([binary, '--serve'],
                env=dict(env, SOPHIA_SHELL_9P_SOCKET=auth_path, SOPHIA_LAUNCH_AUTH_FD=value),
                capture_output=True, timeout=2)
            assert refused.returncode == 1 and b'stage=authentication' in refused.stderr
            try:
                connection, _ = listener.accept()
            except BlockingIOError:
                pass
            else:
                connection.close()
                raise AssertionError('invalid launch descriptor reached the role socket')

    for mode in ('reopen', 'image_upload', 'wrong_revision', 'startup_timeout'):
        path = str(Path(directory) / (mode + '.sock'))
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
            listener.bind(path)
            listener.listen(1)
            listener.settimeout(15)
            child = subprocess.Popen([binary, '--serve'], env=dict(env, SOPHIA_SHELL_9P_SOCKET=path),
                                     stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            peer = None
            try:
                with listener.accept()[0] as socket_peer:
                    socket_peer.settimeout(10)
                    if mode == 'startup_timeout':
                        size, kind, _ = unpack('IBH', exact(socket_peer, 7))
                        assert kind == 100 and exact(socket_peer, size - 7)[6:] == b'9P2000.L'
                        # Withhold the version response; the client must end its wait.
                        stdout, stderr = child.communicate(timeout=8)
                        assert child.returncode == 1 and b'status=failed' in stderr
                    else:
                        peer = Peer(socket_peer, root, wrong_revision=mode == 'wrong_revision',
                                    upload=mode == 'image_upload')
                        deadline = time.monotonic() + 15
                        try:
                            while not peer.finished:
                                assert time.monotonic() < deadline, '9P fixture deadline'
                                peer.step()
                        except (EOFError, ConnectionResetError, BrokenPipeError):
                            assert mode == 'wrong_revision'
                        if mode == 'reopen':
                            assert peer.allocations == 2
                            assert peer.image_ready
                        if mode == 'image_upload':
                            assert peer.source_received
                        if mode in ('reopen', 'image_upload'):
                            child.terminate()
                        stdout, stderr = child.communicate(timeout=5)
                        if mode == 'wrong_revision':
                            assert child.returncode == 1 and b'status=failed' in stderr
                            assert b'status=negotiated' not in stderr
                        else:
                            assert child.returncode == 0, stderr
                            assert stderr.count(b'status=negotiated revision=10 epoch=11 wire=9p') == 1
                            assert b'status=failed' not in stderr
                    assert not stdout
            finally:
                if peer is not None:
                    peer.close()
                if child.poll() is None:
                    child.kill()
                    child.communicate(timeout=5)
print('bemenu_executable private_9p=pass openings=2 startup_timeout=pass ipc_linked=false native=false')
