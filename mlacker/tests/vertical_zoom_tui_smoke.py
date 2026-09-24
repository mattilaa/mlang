"""Zoom, edit intermediate notes, and return to the normal pattern view."""
from session_tui_smoke import Terminal, F1


def main():
    tui = Terminal()
    try:
        tui.read(.8)
        tui.send(F1 + b"lll" + b"jj\r")
        tui.send(b"\t")
        frame = tui.send(b"\x1a")
        assert b"1/32" in frame, frame[-4000:]
        frame = tui.send(b"\x1a")
        assert b"1/64" in frame, frame[-4000:]
        for pitch in (b"C-4", b"D-4", b"E-4", b"F-4"):
            frame = tui.send(b"\r\x15" + pitch + b"\r")
            assert pitch in frame, frame[-4000:]
            tui.send(b"j")
        # Return to the last subdivision and edit the existing note.
        frame = tui.send(b"k\r")
        assert b"F-4" in frame, frame[-4000:]
        tui.send(b"\x15G-4\r")
        # Copy a single subrow to the following main row, then cut/paste it
        # between main rows. None of these operations should copy row-zero C-4.
        tui.send(b"vyjp")
        frame = tui.send(b"\r")
        assert b"G-4" in frame, frame[-4000:]
        tui.send(b"\r")
        tui.send(b"vdjp")
        frame = tui.send(b"\r")
        assert b"G-4" in frame, frame[-4000:]
        tui.send(b"\r")
        frame = tui.send(b"\x1a")
        assert b"1/16" in frame and b"1/64" not in frame, frame[-4000:]
        # Enhanced terminal Ctrl-Z follows the same path.
        frame = tui.send(b"\x1b[122;5u")
        assert b"1/32" in frame, frame[-4000:]
        frame = tui.send(b"o\r\x15A-4\r")
        assert b"A-4" in frame, frame[-4000:]
        frame = tui.send(b"dd")
        assert b"A-4" not in frame and b"1/32" in frame, frame[-4000:]
        frame = tui.send(b"O\r\x15B-4\r")
        assert b"B-4" in frame, frame[-4000:]
    finally:
        tui.close()


if __name__ == "__main__":
    main()
