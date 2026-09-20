"""Pattern insert selection, editing, removal, and session restoration."""
import os
import sys
import tempfile
from pathlib import Path
from session_tui_smoke import Terminal

# The menu bar opens with F1; Tab cycles panes.
F1 = b"\x1bOP"


def main():
    with tempfile.TemporaryDirectory(prefix="mlacker-inserts-") as folder:
        path = Path(folder) / "inserts.mlack"
        tui = Terminal()
        try:
            tui.read(.8)
            tui.send(F1 + b"llljljj\r")  # Instrument track.
            tui.send(b"\x1b[108;6u")  # Focus Pattern view.
            frame = tui.send(b"f")
            assert b"- empty -" in frame, frame[-5000:]
            frame = tui.send(b"\r")
            assert b"Load VST3 insert effect" in frame
            frame = tui.send(b"\x15" + os.fsencode(os.path.abspath(sys.argv[2])) + b"\r", .8)
            assert b"Insert effect loaded" in frame
            frame = tui.send(b"\r")
            assert b"VST3 editor:" in frame
            assert b"0.25" in tui.send(b"\r\x150.25\r")
            tui.send(b"\x1b")
            frame = tui.send(F1 + b"llllllljj\r")  # Effect > Edit effect plugin.
            assert b"0.25" in frame, frame[-5000:]
            tui.send(b"\x1b")
            tui.send(b"j\r")
            tui.send(b"\x15" + os.fsencode(os.path.abspath(sys.argv[2])) + b"\r", .8)
            tui.send(b"k")
            tui.send(b"\x13")
            assert b"Saved:" in tui.send(b"\x15" + os.fsencode(path) + b"\r", .6)
        finally:
            tui.close()
        saved = path.read_bytes()
        assert b"TRACK_INSERTS" in saved
        tui = Terminal(str(path))
        try:
            frame = tui.read(.8)
            assert b"Mlacker" in frame, frame[-6000:]
            tui.send(b"\x13", .6)
            assert path.read_bytes() == saved
            assert b"0.25" in tui.send(b"\r")
            tui.send(b"\x1b")
            # Replacing the audio device must keep insert parameter state.
            assert b"MIDI input adapter" in tui.send(F1 + b"jjjj\r")
            tui.send(b"\rk\r"); tui.send(b"\t\rk\r")
            assert b"Settings applied. Audio disabled." in tui.send(b"\t\r", .6)
            assert b"0.25" in tui.send(b"\r")
            tui.send(b"\x1b")
            tui.send(b"\x7f")
            assert b"Load VST3 insert effect" in tui.send(b"\r")
            tui.send(b"\x1b")
            assert b"VST3 editor:" in tui.send(b"j\r")
            tui.send(b"\x1b")
            tui.send(b"k\r")
            frame = tui.send(b"\x15" + os.fsencode(os.path.abspath(sys.argv[2])) + b"\r", .8)
            assert b"Insert effect loaded" in frame
            tui.send(b"f")
        finally:
            tui.close()
    print("PASS: insert rows, load/edit/remove, parameter and session restoration")


if __name__ == "__main__":
    main()
