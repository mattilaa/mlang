"""Record menu toggles and one-bar count-in through the real terminal UI."""
from session_tui_smoke import Terminal, F1


def main():
    tui = Terminal()
    record = F1 + b"l" * 8
    try:
        tui.read(.8)
        frame = tui.send(record)
        assert b"[x] Play metronome" in frame and b"[x] Extend pattern when playing" in frame
        frame = tui.send(b"jjl")
        assert b"[x] On recording" in frame and b"[ ] Always" in frame
        tui.send(b"j\r")
        frame = tui.send(record + b"jjl")
        assert b"[ ] On recording" in frame and b"[x] Always" in frame
        tui.send(b"\r")  # Restore the default mode.
        frame = tui.send(record + b"jjl")
        assert b"[x] On recording" in frame and b"[ ] Always" in frame
        tui.send(b"\x1b")
        tui.send(b"\x1b")
        tui.send(record + b"j\r")
        frame = tui.send(record)
        assert b"[ ] Extend pattern when playing" in frame
        tui.send(b"\x1b")
        tui.send(F1 + b"lll" + b"jj\r")
        tui.send(b"m\t\tR")
        frame = tui.send(b" ", .4)
        assert b"COUNT IN" in frame, frame[-4000:]
        frame = tui.read(2)
        assert b"REC" in frame, frame[-4000:]
        tui.send(b" ")
        tui.send(b"\r")  # Accept the recorded track name.
        tui.send(record + b"\r")
        frame = tui.send(record)
        assert b"[ ] Play metronome" in frame
        tui.send(b"\x1b")
        frame = tui.send(b" ", .4)
        assert b"REC" in frame and b"COUNT IN" not in frame, frame[-4000:]
        tui.send(b" ")
        tui.send(b"\r")
    finally:
        tui.close()


if __name__ == "__main__":
    main()
