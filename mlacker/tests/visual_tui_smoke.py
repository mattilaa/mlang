"""Pattern visual selection and modal paste errors in a hardware-free PTY."""
from session_tui_smoke import Terminal


class VisualTerminal(Terminal):
    def read(self, seconds=0.35):
        # Button presses include animation frames before the final repaint.
        return super().read(seconds).rsplit(b" File  Edit ", 1)[-1]


def main():
    tui = VisualTerminal()
    try:
        tui.read(0.8)
        tui.send(b"\tllljl\r")  # Track > Create track > MIDI
        frame = tui.send(b"\x1b[108;6uK")  # Pattern pane, create a note
        assert b"C-4" in frame and b"100" in frame, frame[-5000:]
        tui.send(b"vly")
        frame = tui.send(b"lp")  # NOTE clipboard cannot start on VEL
        assert b"Cannot paste selection" in frame and b"NOTE to NOTE" in frame, frame[-5000:]
        assert b"OK" in frame and b"Cancel" not in frame, frame[-5000:]
        frame = tui.send(b"jklh")
        assert b"Cannot paste selection" in frame, frame[-5000:]
        frame = tui.send(b"\r", 0.6)
        assert b"Cannot paste selection" not in frame, frame[-5000:]
        frame = tui.send(b"hjp")  # matching NOTE in the following row
        assert b"Cannot paste selection" not in frame and frame.count(b"C-4") >= 2, frame[-5000:]
        frame = tui.send(b"vlkd")  # clear both rows, not delete pattern rows
        assert b"C-4" not in frame and b"003" in frame, frame[-5000:]
        frame = tui.send(b"p")
        assert frame.count(b"C-4") >= 2, frame[-5000:]
        tui.send(b"v")
        frame = tui.send(b"\x1b")
        assert b"New session" not in frame, "Visual Escape opened a menu"
        tui.send(b"v")
        assert b"New session" in tui.send(b"\t")
        assert b"New session" not in tui.send(b"\x1b"), "Visual selection stole Escape from the menu"
        tui.send(b"\x1b")
    finally:
        tui.close()
    print("PASS: visual copy/cut/paste, matching-column OK modal, new-note velocity, Escape")


if __name__ == "__main__":
    main()
