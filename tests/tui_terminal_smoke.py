"""Exercise the built TUI demo in a real PTY, without third-party packages.

Usage: python3 tests/tui_terminal_smoke.py /tmp/mlang_tui_demo
"""
import fcntl
import os
import re
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
    pending_output = bytearray()

    def read_until(needle, timeout=5):
        nonlocal pending_output
        data = pending_output
        pending_output = bytearray()
        deadline = time.monotonic() + timeout
        while needle not in data and time.monotonic() < deadline:
            ready, _, _ = select.select([master], [], [], 0.1)
            if ready:
                data.extend(os.read(master, 65536))
            elif process.poll() is not None:
                break
        assert needle in data, f"Missing {needle!r}; output tail: {data[-300:]!r}"
        end = data.index(needle) + len(needle)
        pending_output = data[end:]
        return data[:end]

    def read_frame(expected_pane, dialog_pane=None):
        frame = read_until(b"\x1b[0m")
        # Inspect rendered border colors, not application debug/status text.
        x = y = 0
        foreground = None
        cells = {}
        for token in re.split(r"(\x1b\[[0-?]*[ -/]*[@-~])", frame.decode()):
            if token.startswith("\x1b["):
                params = token[2:-1]
                if token.endswith("H"):
                    row, col = map(int, params.split(";"))
                    x, y = col - 1, row - 1
                elif token.endswith("m") and params.startswith("38;2;"):
                    foreground = tuple(map(int, params.split(";")[2:]))
            else:
                for _glyph in token:
                    cells[x, y] = foreground
                    x += 1
        borders = [(0, 8), (23, 8), (23, 18)]
        selected = [i for i, point in enumerate(borders) if cells.get(point) == (145, 184, 235)]
        assert selected == ([] if expected_pane is None else [expected_pane]), selected
        if dialog_pane is not None:
            dialog_borders = [(3, 3), (3, 8), (28, 8)]
            dialog_selected = [i for i, point in enumerate(dialog_borders)
                               if cells.get(point) == (145, 184, 235)]
            assert dialog_selected == [dialog_pane], dialog_selected
        return frame

    try:
        frame = read_frame(None)
        assert b"\x1b[?1049h" in frame and b"\x1b[?25l" in frame
        assert b"38;2;" in frame and b"48;2;" in frame
        assert "┌".encode() in frame and b"New session" in frame
        assert b"\x1b[>1u" in frame
        # Switch to Edit, activate Undo, then reopen and dismiss with Escape.
        os.write(master, b"\x1b[C")
        assert b"Undo" in read_frame(None)
        os.write(master, b"\r")
        assert b"Command selected" in read_frame(0)
        # Ctrl+Shift+L/J/K/H traverses the nested pane geometry.
        for packet, pane in [(b"\x1b[108;6u", 1), (b"\x1b[106;6u", 2),
                             (b"\x1b[107;6u", 1), (b"\x1b[104;6u", 0),
                             (b"\x1b[108;6u", 1), (b"\x1b[106;6u", 2)]:
            os.write(master, packet)
            read_frame(pane)
        os.write(master, b"\t")
        assert b"New session" in read_frame(None)
        os.write(master, b"\x1b[107;6u")
        read_frame(None)  # menu retains keyboard ownership
        os.write(master, b"\x1b")
        read_frame(2)  # previous pane restored, not the first pane
        for dismissal in (b"\t", b"\r"):
            os.write(master, b"\t")
            read_frame(None)
            os.write(master, dismissal)
            read_frame(2)
        # Cascades remain beside their ancestors. h closes only one level;
        # Escape from the grandchild closes every menu and restores pane 2.
        os.write(master, b"\t")
        read_frame(None)
        os.write(master, b"jjl")
        cascade = read_frame(None)
        assert b"Recent sessions" in cascade and b"Blue hour" in cascade and b">" in cascade
        os.write(master, b"jl")
        assert b"Ambient" in read_frame(None)
        os.write(master, b"h")
        parent = read_frame(None)
        assert b"Blue hour" in parent and b"Ambient" not in parent
        os.write(master, b"l")
        assert b"Ambient" in read_frame(None)
        os.write(master, b"\x1b")
        closed = read_frame(2)
        assert b"Blue hour" not in closed and b"Ambient" not in closed
        # File -> Open session creates a modal browser, not a status-only action.
        os.write(master, b"\t")
        read_frame(None)
        os.write(master, b"j\r")
        browser = read_frame(None, 0)
        assert b"Open session" in browser and b"Path: " in browser
        assert b"Directories" in browser and b"Files" in browser
        os.write(master, b"\x15tests/fixtures/tui_dialog\r")
        browser = read_frame(None, 0)
        assert b"alpha.session" in browser and b"qhjk session.session" in browser
        os.write(master, b"\x1b[106;6u")  # path -> files
        read_frame(None, 2)
        os.write(master, b"\x1b[104;6u")  # files -> directories
        read_frame(None, 1)
        os.write(master, b"jl")  # select branch, expand it
        browser = read_frame(None, 1)
        assert b"child.session" in browser and b"nested" in browser
        os.write(master, b"\x1b[108;6u")
        read_frame(None, 2)
        os.write(master, b"\x1b[107;6u")
        read_frame(None, 0)
        os.write(master, b"\x1b[106;6u\r")
        assert b"Selected:" in read_frame(2)
        # An invalid typed path keeps the dialog open, even when it contains q.
        os.write(master, b"\tj\r")
        read_frame(None, 0)
        os.write(master, b"\x15qhjk-does-not-exist.session\r")
        assert b"inaccessible" in read_frame(None, 0)
        os.write(master, b"\x15tests/fixtures/tui_dialog/qhjk session.session\r")
        assert b"Selected:" in read_frame(2)
        # Cancellation restores pane focus and small viewports remain usable.
        os.write(master, b"\tj\r")
        read_frame(None, 0)
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 6, 12, 0, 0))
        read_until(b"\x1b[0m")
        os.write(master, b"\x1b")
        read_until(b"\x1b[0m")
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
        read_frame(0)  # prior pane collapsed on resize; visible fallback is Library
        # A resize causes re-layout and a full frame with the new bottom row.
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 12, 45, 0, 0))
        read_until(b"\x1b[12;1H")
        os.write(master, b"q")
        cleanup = read_until(b"\x1b[?1049l")
        assert b"\x1b[<u" in cleanup
        assert process.wait(timeout=5) == 0
        after = termios.tcgetattr(slave)
        # macOS may set PENDIN when returning to canonical input; it is pending
        # input bookkeeping, not a raw-mode setting controlled by the program.
        pending = getattr(termios, "PENDIN", 0)
        before[3] &= ~pending
        after[3] &= ~pending
        assert after == before, f"Terminal settings were not restored: before={before!r}, after={after!r}"
        print("PASS: Open session dialog, path editing, directory browsing, modal focus, submenus, resize, terminal restoration")
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
        os.close(master)
        os.close(slave)


if __name__ == "__main__":
    main()
