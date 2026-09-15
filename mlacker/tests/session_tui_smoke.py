"""Real default startup and .mlack/VST3 session round trips, no audio hardware."""
import fcntl
import os
import re
import select
import struct
import subprocess
import sys
import tempfile
import termios
import time
from pathlib import Path


class Terminal:
    def __init__(self, *args):
        self.master, self.slave = os.openpty()
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack("HHHH", 28, 120, 0, 0))
        env = dict(os.environ, TERM="xterm-256color", MLANG_TUI_NO_HARDWARE="1")
        env.pop("MLANG_TUI_DEMO", None)
        env.pop("NO_COLOR", None)
        self.process = subprocess.Popen([sys.argv[1], *args], stdin=self.slave, stdout=self.slave, stderr=self.slave, env=env)

    def read(self, seconds=0.35):
        data = bytearray()
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if select.select([self.master], [], [], 0.03)[0]:
                data.extend(os.read(self.master, 65536))
        return re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", data)

    def send(self, keys, seconds=0.35):
        os.write(self.master, keys)
        return self.read(seconds)

    def close(self):
        if self.process.poll() is None:
            self.send(b"\x1b")
            self.send(b"q")
        try:
            assert self.process.wait(timeout=3) == 0
        finally:
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait()
            os.close(self.master)
            os.close(self.slave)


def main():
    with tempfile.TemporaryDirectory(prefix="mlacker-session-") as directory:
        path = Path(directory) / "round trip.mlack"
        tui = Terminal()
        try:
            frame = tui.read(0.8)
            assert b"Untitled" in frame and b"New session" not in frame and b"C-4" not in frame, frame[-4000:]
            frame = tui.send(b"\x13")
            assert b"Save session (.mlack)" in frame, frame[-4000:]
            frame = tui.send(b"\x15" + os.fsencode(path) + b"\r", 0.7)
            assert b"Saved:" in frame, frame[-4000:]
            assert path.read_bytes().startswith(struct.pack("<q", 5) + b"MLACK" + struct.pack("<qq", 1, 0))
            assert b"Instrument track created" in tui.send(b"\tllljljj\r")
            assert b"Add VST3 instrument" in tui.send(b"\tllllljjj\r")
            frame = tui.send(b"\x15" + os.fsencode(os.path.abspath(sys.argv[2])) + b"\r", 0.9)
            assert b"Instrument loaded:" in frame, frame[-4000:]
            assert b"VST3 editor:" in tui.send(b"\tllllll\r")
            assert b"0.25" in tui.send(b"\r\x150.25\r")
            frame = tui.send(b"\x13", 0.6)
            assert b"Saved:" in frame, frame[-4000:]
        finally:
            tui.close()
        saved = path.read_bytes()
        tui = Terminal(str(path))
        try:
            frame = tui.read(1.0)
            assert b"VST3 editor:" in frame and b"0.25" in frame and b"New session" not in frame, frame[-5000:]
            tui.send(b"\x13", 0.6)
            assert path.read_bytes() == saved, "Reopened session changed on save"
            tui.send(b"\x1b")
            tui.send(b"\x13", 0.6)
            closed_editor = path.read_bytes()
            bad = Path(directory) / "bad.mlack"
            bad.write_bytes(saved[:20])
            assert b"Open session" in tui.send(b"\tj\r")
            frame = tui.send(b"\x15" + os.fsencode(bad) + b"\r", 0.6)
            assert b"truncated" in frame and b"Instrument: 1" in frame, frame[-5000:]
        finally:
            tui.close()
        tui = Terminal(str(path))
        try:
            frame = tui.read(1.0)
            assert b"VST3 editor:" not in frame and b"Instrument: 1" in frame, frame[-5000:]
            assert b"001 Mlacker Test Ins" in frame, frame[-5000:]
            tui.send(b"\x13", 0.6)
            assert path.read_bytes() == closed_editor, "Closed editor state changed on reload"
            frame = tui.send(b"\tllllll\r")
            assert b"VST3 editor: Mlacker Test Instrument" in frame and b"0.25" in frame, frame[-5000:]
            tui.send(b"\x1b")
            assert b"MIDI input adapter" in tui.send(b"\tjjjj\r")
            tui.send(b"\rk\r")
            tui.send(b"\t\rk\r")
            frame = tui.send(b"\t\r", 0.6)
            assert b"Settings applied. Audio disabled." in frame, frame[-5000:]
            frame = tui.send(b"\tllllll\r")
            assert b"VST3 editor: Mlacker Test Instrument" in frame and b"0.25" in frame, frame[-5000:]
        finally:
            tui.close()
        master_path = Path(directory) / "master.mlack"
        tui = Terminal()
        try:
            tui.read(0.8)
            assert b"Load master VST3" in tui.send(b"\tlllllj\r")
            frame = tui.send(b"\x15" + os.fsencode(os.path.abspath(sys.argv[2])) + b"\r", 0.9)
            assert b"Master VST3: Mlacker Test Instrument" in frame, frame[-5000:]
            tui.send(b"\x13")
            assert b"Saved:" in tui.send(b"\x15" + os.fsencode(master_path) + b"\r", 0.6)
        finally:
            tui.close()
        master_saved = master_path.read_bytes()
        tui = Terminal(str(master_path))
        try:
            frame = tui.read(1.0)
            assert b"Opened:" in frame, frame[-5000:]
            tui.send(b"\x13", 0.6)
            assert master_path.read_bytes() == master_saved, "Master plugin state changed on reload"
            assert b"MIDI input adapter" in tui.send(b"\tjjjj\r")
            tui.send(b"\rk\r")
            tui.send(b"\t\rk\r")
            frame = tui.send(b"\t\r", 0.6)
            assert b"Settings applied. Audio disabled." in frame, frame[-5000:]
            tui.send(b"\x13", 0.6)
            assert master_path.read_bytes() == master_saved, "Master plugin state changed on output replacement"
            assert b"Master VST3 unloaded" in tui.send(b"\tllllljj\r")
        finally:
            tui.close()
        unsupported = Path(directory) / "future.mlack"
        unsupported.write_bytes(saved[:13] + struct.pack("<q", 2) + saved[21:])
        tui = Terminal(str(unsupported))
        try:
            frame = tui.read(0.8)
            assert b"unsupported" in frame and b"Untitled" in frame and b"VST3 editor:" not in frame, frame[-5000:]
        finally:
            tui.close()
        missing = Path(directory) / "missing.mlack"
        plugin = os.fsencode(os.path.abspath(sys.argv[2]))
        missing.write_bytes(saved.replace(plugin, b"/" + b"x" * (len(plugin) - 1)))
        tui = Terminal(str(missing))
        try:
            frame = tui.read(0.8)
            assert b"missing or could not load" in frame and b"Untitled" in frame, frame[-5000:]
        finally:
            tui.close()
    print("PASS: empty startup, atomic save, CLI load, VST3 parameter/editor/master state, corrupt/version/missing-plugin rejection")


if __name__ == "__main__":
    main()
