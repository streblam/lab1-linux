#!/usr/bin/env python3
"""Linux integration harness; Python is used only for tests, never by the app."""
import os
import pathlib
import re
import shutil
import signal
import subprocess
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
BINARY = pathlib.Path(os.environ.get('OSLAB_BINARY', ROOT / 'oslab')).resolve()
EVIDENCE = pathlib.Path(os.environ.get('OSLAB_EVIDENCE', ROOT / 'docs' / 'test-evidence'))
EVIDENCE.mkdir(parents=True, exist_ok=True)
passed = []

class Case:
    def __init__(self, name, workers=3):
        self.name = name
        self.tmp = tempfile.TemporaryDirectory(prefix='oslab-test-')
        self.root = pathlib.Path(self.tmp.name)
        self.data = self.root / 'data'
        self.data.mkdir()
        self.configdir = self.root / '.config' / 'tsi-os-lab1'
        self.configdir.mkdir(parents=True)
        self.config = self.configdir / 'config.ini'
        self.log = self.root / 'supervisor.log'
        self.bin = self.root / 'oslab'
        shutil.copy2(BINARY, self.bin)
        self.env = dict(os.environ, HOME=str(self.root))
        self.config.write_text('[supervisor]\nworkers=%d\ndirectory=%s\nlog=%s\n[priorities]\n%s' % (workers, self.data, self.log, ''.join('worker%d=NORMAL\n' % i for i in range(1, workers + 1))))
        self.p = None
    def proc_for(self, pid):
        # /proc can be mounted in an ancestor PID namespace (containers).
        for entry in pathlib.Path("/proc").iterdir():
            if not entry.name.isdigit():
                continue
            try:
                status = (entry / "status").read_text()
                match = re.search(r"NSpid:\s*([0-9 \t]+)", status)
                local_pid = match.group(1).split()[-1] if match else entry.name
                args = (entry / "cmdline").read_bytes()
                if local_pid == str(pid) and (b"--worker" in args or str(self.bin).encode() in args or re.search(r"Name:\s*oslab(?:-ubsan)?\n", status)):
                    return entry
            except (FileNotFoundError, ProcessLookupError, PermissionError):
                pass
        return None
    def text(self):
        return self.log.read_text() if self.log.exists() else ''
    def waitfor(self, pattern, seconds=10):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            match = re.search(pattern, self.text())
            if match:
                return match
            if self.p.poll() is not None:
                raise AssertionError('exited before ' + pattern + '\n' + self.text())
            time.sleep(.02)
        raise AssertionError('timeout waiting for ' + pattern + '\n' + self.text())
    def start(self, *args, unprivileged=False):
        preexec = None
        caps = int(re.search(r'CapEff:\s*(\w+)', pathlib.Path('/proc/self/status').read_text()).group(1), 16)
        if unprivileged and os.geteuid() == 0 and caps & (1 << 23):
            for p in [self.root, *self.root.rglob('*')]:
                os.chown(p, 65534, 65534)
            def demote():
                os.setgroups([])
                os.setgid(65534)
                os.setuid(65534)
            preexec = demote
        self.p = subprocess.Popen([str(self.bin), *args], env=self.env, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, preexec_fn=preexec)
    def command(self, command):
        self.p.stdin.write(command + '\n')
        self.p.stdin.flush()
    def finish(self, expected=0):
        _, err = self.p.communicate(timeout=15)
        text = self.text()
        assert self.p.returncode == expected, (self.name, self.p.returncode, err, text)
        assert 'runtime error:' not in err, err
        for pid in re.findall(r'START worker=\d+ pid=(\d+)', text):
            assert self.proc_for(pid) is None, ('child left alive or zombie', pid)
        (EVIDENCE / (self.name + '.log')).write_text(text + ('\nSTDERR:\n' + err if err else ''))
        passed.append(self.name)
        print('PASS', self.name, flush=True)
    def cleanup(self):
        if self.p and self.p.poll() is None:
            # Parent SIGTERM exercises its cleanup even when an assertion fails.
            self.p.terminate()
            try:
                self.p.communicate(timeout=6)
            except subprocess.TimeoutExpired:
                self.p.kill()
                self.p.communicate()
        self.tmp.cleanup()

def run(name, fn, workers=3):
    c = Case(name, workers)
    try:
        fn(c)
    finally:
        c.cleanup()

def reference(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xffffffff
    lines = data.count(b'\n') + int(bool(data) and not data.endswith(b'\n'))
    return len(data), lines, h

def normal(c):
    files = {'empty': b'', 'spaces and ā.txt': b'a\nb\n', 'no-final-newline': b'abc', 'binary': bytes(range(256)) * 5000, 'tab\tnewline\nname': b'\x00\xff\n', 'quote"file': b'123456789'}
    for name, data in files.items():
        (c.data / name).write_bytes(data)
    (c.data / 'subdir').mkdir()
    (c.data / 'link').symlink_to(c.data / 'empty')
    os.mkfifo(c.data / 'fifo')
    c.start()
    c.finish()
    results = re.findall(r'RECV worker=\d+ RESULT (\d+) (\d+) (\d+) path_hex=(\S+)', c.text())
    assert len(results) == len(files), c.text()
    for b, l, h, path in results:
        name = pathlib.Path(bytes.fromhex(path).decode()).name
        assert (int(b), int(l), int(h)) == reference(files[name])
    assert len(re.findall(r'RECV worker=\d+ READY', c.text())) in (3, 5)

def priority(c):
    c.start('--interactive', unprivileged=True)
    c.waitfor('IDLE')
    pids = re.findall(r'START worker=\d+ pid=(\d+)', c.text())
    initial_fds = {pid: len(list((c.proc_for(pid) / 'fd').iterdir())) for pid in pids}
    parent_fds = len(list((c.proc_for(c.p.pid) / 'fd').iterdir()))
    c.command('priority 1 LOW')
    c.waitfor('RECV worker=1 PRIORITY LOW 10')
    c.command('priority 1 HIGH')
    c.waitfor('RECV worker=1 ERROR PRIORITY')
    c.command('priority 1 NORMAL')
    c.waitfor('COMMAND worker=1 SET_PRIORITY NORMAL')
    c.command('priority 99 LOW')
    c.waitfor('ERROR CONSOLE')
    c.command('status')
    c.waitfor('STATUS worker=3')
    for pid in pids:
        assert len(list((c.proc_for(pid) / 'fd').iterdir())) == initial_fds[pid]
        assert initial_fds[pid] == 4, initial_fds
    assert len(list((c.proc_for(c.p.pid) / 'fd').iterdir())) == parent_fds
    assert parent_fds == 7, parent_fds
    print('FD_CHECK: each idle Worker = 4; Supervisor = 7; unchanged after console commands', flush=True)
    c.command('shutdown')
    c.finish()
    assert len(re.findall('ERROR PRIORITY', c.text())) == 2

def crash(c):
    for i in range(12):
        (c.data / str(i)).write_text('work\n')
    c.start('--interactive', '--delay-ms', '250')
    c.waitfor('COMMAND worker=1 PROCESS')
    pid = int(c.waitfor(r'START worker=1 pid=(\d+)').group(1))
    os.kill(pid, signal.SIGKILL)
    c.waitfor('UNEXPECTED_EXIT worker=1')
    c.waitfor('IDLE')
    c.command('shutdown')
    c.finish(2)
    assert 'REQUEUE' in c.text()
    assert 'SUMMARY completed=12 failed=0 pending=0 children_alive=0' in c.text()

def missing(c):
    file = c.data / 'remove-me'
    file.write_text('hello')
    c.start('--delay-ms', '500')
    c.waitfor('PROCESS ')
    file.unlink()
    c.finish(2)
    assert 'ERROR PROCESS 2 ' in c.text()

def signal_stop(c):
    for i in range(10):
        (c.data / str(i)).write_text('test')
    c.start('--delay-ms', '250')
    c.waitfor('PROCESS ')
    c.p.send_signal(signal.SIGTERM)
    c.finish(2)
    assert 'SHUTDOWN_BEGIN' in c.text()

def shutdown_timeout(c):
    c.start('--interactive')
    c.waitfor('IDLE')
    pid = int(c.waitfor(r'START worker=1 pid=(\d+)').group(1))
    os.kill(pid, signal.SIGSTOP)
    c.command('shutdown')
    c.finish(2)
    assert 'SHUTDOWN_TIMEOUT worker=1' in c.text()

def invalid(c):
    c.config.write_text(c.config.read_text().replace('workers=3', 'workers=2'))
    c.start()
    c.finish(1)
    assert not c.log.exists()

def init(c):
    c.config.unlink()
    result = subprocess.run([str(c.bin), '--init'], cwd=c.root, env=c.env, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert 'workers=3' in c.config.read_text()
    before = c.config.read_bytes()
    result = subprocess.run([str(c.bin), '--init'], cwd=c.root, env=c.env, capture_output=True)
    assert result.returncode == 1 and c.config.read_bytes() == before
    c.start()
    c.finish()

def all_dead(c):
    c.start('--interactive')
    c.waitfor('IDLE')
    for pid in re.findall(r'START worker=\d+ pid=(\d+)', c.text()):
        os.kill(int(pid), signal.SIGKILL)
    c.finish(2)
    assert c.text().count('UNEXPECTED_EXIT') == 3

def empty(c):
    c.start()
    c.finish()
    assert 'completed=0 failed=0 pending=0' in c.text()

def task_timeout(c):
    (c.data / 'slow').write_text('slow')
    c.start('--delay-ms', '2000', '--timeout-seconds', '1')
    c.finish(2)
    assert 'ERROR TIMEOUT' in c.text() and 'ERROR NO_WORKERS remaining=1' in c.text()

if __name__ == '__main__':
    print('Binary:', BINARY)
    print('Kernel:', os.uname().release, os.uname().machine)
    for name, fn, n in [('normal-3-workers', normal, 3), ('normal-5-workers', normal, 5), ('priority-permissions-fd-count', priority, 3), ('worker-sigkill-requeue', crash, 3), ('file-removed', missing, 3), ('supervisor-sigterm', signal_stop, 3), ('shutdown-timeout', shutdown_timeout, 3), ('invalid-config', invalid, 3), ('init-no-overwrite', init, 3), ('all-workers-dead', all_dead, 3), ('empty-directory', empty, 3), ('task-timeout', task_timeout, 3)]:
        run(name, fn, n)
    print('%d/%d PASS' % (len(passed), 12))
