"""Exercise the built TUI demo in a real PTY, without third-party packages.

Usage: python3 tests/tui_terminal_smoke.py /tmp/mlang_tui_demo
"""
import fcntl
import os
import select
import struct
import subprocess
import sys
import termios
import time


def main():
    master, slave = os.openpty()
    before = termios.tcgetattr(slave)
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
    env = dict(os.environ, TERM="xterm-256color", COLORTERM="truecolor")
    env.pop("NO_COLOR", None)
    process = subprocess.Popen([sys.argv[1]], stdin=slave, stdout=slave, stderr=slave, env=env)

    def read_until(needle, timeout=5):
        data = bytearray()
        deadline = time.monotonic() + timeout
        while needle not in data and time.monotonic() < deadline:
            ready, _, _ = select.select([master], [], [], 0.1)
            if ready:
                data.extend(os.read(master, 65536))
            elif process.poll() is not None:
                break
        assert needle in data, f"Missing {needle!r}; output tail: {data[-300:]!r}"
        return data

    try:
        frame = read_until(b"\x1b[0m")
        assert b"\x1b[?1049h" in frame and b"\x1b[?25l" in frame
        assert b"38;2;" in frame and b"48;2;" in frame
        assert "┌".encode() in frame and b"New session" in frame
        # Switch to Edit, activate Undo, then reopen and dismiss with Escape.
        os.write(master, b"\x1b[C")
        read_until(b"Undo")
        os.write(master, b"\r")
        read_until(b"Command selected")
        os.write(master, b"\t")
        read_until(b"New session")
        os.write(master, b"\x1b")
        read_until(b"Library")
        # A resize causes re-layout and a full frame with the new bottom row.
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 12, 45, 0, 0))
        read_until(b"\x1b[12;1H")
        os.write(master, b"q")
        read_until(b"\x1b[?1049l")
        assert process.wait(timeout=5) == 0
        after = termios.tcgetattr(slave)
        # macOS may set PENDIN when returning to canonical input; it is pending
        # input bookkeeping, not a raw-mode setting controlled by the program.
        pending = getattr(termios, "PENDIN", 0)
        before[3] &= ~pending
        after[3] &= ~pending
        assert after == before, f"Terminal settings were not restored: before={before!r}, after={after!r}"
        print("PASS: colors, layered menu, keyboard actions, resize, terminal restoration")
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
        os.close(master)
        os.close(slave)


if __name__ == "__main__":
    main()
