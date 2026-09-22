"""Hardware-free PTY coverage for File > Settings and public dropdowns."""
import fcntl
import os
import re
import select
import struct
import subprocess
import sys
import termios
import time

# The menu bar opens with F1; Tab cycles panes.
F1 = b"\x1bOP"


def main():
    master, slave = os.openpty()
    before = termios.tcgetattr(slave)
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
    env = dict(os.environ, TERM="xterm-256color", MLANG_TUI_NO_HARDWARE="1")
    env.pop("NO_COLOR", None)
    process = subprocess.Popen([sys.argv[1]], stdin=slave, stdout=slave, stderr=slave, env=env)

    def read_for(seconds=0.25):
        data = bytearray()
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            ready, _, _ = select.select([master], [], [], min(0.05, max(0, deadline - time.monotonic())))
            if ready:
                data.extend(os.read(master, 65536))
        return bytes(data)

    def send(keys):
        os.write(master, keys)
        return read_for()

    try:
        assert b"Settings" in read_for(1)
        # Save is disabled: New -> Open -> Recent -> Settings.
        frame = send(b"jjj\r")
        assert b"MIDI input adapter" in frame and b"Master output (AUHAL)" in frame
        frame = send(b"\r")
        assert b"Disabled" in frame and b"System default" in frame, re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", frame).decode()
        # Escape closes only the popup, the next Escape cancels the dialog.
        frame = send(b"\x1b")
        assert b"MIDI input adapter" in frame
        frame = send(b"\x1b")
        assert b"MIDI input adapter" not in frame and b"Patterns" in frame
        # Reopen and select disabled MIDI/output, with no device access on apply.
        send(F1 + b"jjj\r")
        send(b"\rk\r")
        send(b"\t\rk\r")
        frame = send(b"\t\rjj\r")  # 128 -> 512 frames
        assert b"512 frames" in frame
        frame = send(b"\t\r")
        assert b"Settings applied. Audio disabled." in frame
        frame = send(F1 + b"jjj\r")
        assert b"MIDI input adapter" in frame and b"Disabled" in frame and b"512 frames" in frame
        # Resize with an expanded dropdown, exercising clipping and overlay.
        send(b"\r")
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 10, 30, 0, 0))
        assert read_for()
        send(b"\x1b")
        send(b"\x1b")
        send(b"q")
        deadline = time.monotonic() + 3
        while process.poll() is None and time.monotonic() < deadline:
            read_for(0.1)
        assert process.poll() == 0
        after = termios.tcgetattr(slave)
        pending = getattr(termios, "PENDIN", 0)
        before[3] &= ~pending
        after[3] &= ~pending
        assert after == before
        print("PASS: Settings dropdowns, cancel, apply disabled devices, resize, shutdown")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)
        os.close(slave)


if __name__ == "__main__":
    main()
