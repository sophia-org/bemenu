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
                'WAYLAND_DISPLAY', 'WAYLAND_SOCKET', 'BEMENU_BACKEND', 'BEMENU_RENDERER'):
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

    for mode in ('reopen', 'wrong_revision', 'startup_timeout'):
        path = str(Path(directory) / (mode + '.sock'))
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
            listener.bind(path)
            listener.listen(1)
            listener.settimeout(15)
            child = subprocess.Popen([binary, '--serve'], env=dict(env, SOPHIA_SHELL_9P_SOCKET=path),
                                     stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
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
                        peer = Peer(socket_peer, root, wrong_revision=mode == 'wrong_revision')
                        deadline = time.monotonic() + 15
                        try:
                            while not peer.finished:
                                assert time.monotonic() < deadline, '9P fixture deadline'
                                peer.step()
                        except (EOFError, ConnectionResetError, BrokenPipeError):
                            assert mode == 'wrong_revision'
                        if mode == 'reopen':
                            assert peer.allocations == 2
                            child.terminate()
                        stdout, stderr = child.communicate(timeout=5)
                        if mode == 'wrong_revision':
                            assert child.returncode == 1 and b'status=failed' in stderr
                            assert b'status=negotiated' not in stderr
                        else:
                            assert child.returncode == 0, stderr
                            assert stderr.count(b'status=negotiated revision=7 epoch=11 wire=9p') == 1
                            assert b'status=failed' not in stderr
                    assert not stdout
            finally:
                if child.poll() is None:
                    child.kill()
                    child.communicate(timeout=5)
print('bemenu_executable private_9p=pass openings=2 startup_timeout=pass ipc_linked=false native=false')
