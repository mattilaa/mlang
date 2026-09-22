"""Aux channel lifecycle, editor, send levels, and session/device round trips."""
import os
import sys
import tempfile
from pathlib import Path
from session_tui_smoke import Terminal

# The menu bar opens with F1; Tab cycles panes.
F1 = b"\x1bOP"


def main():
    with tempfile.TemporaryDirectory(prefix="mlacker-effects-") as folder:
        path = Path(folder) / "effects.mlack"
        tui = Terminal()
        try:
            tui.read(.8)
            assert b"Instrument track created" in tui.send(F1 + b"lll" + b"jj\r")
            tui.send(F1 + b"llllll\r")
            tui.send(b"\x15" + os.fsencode(os.path.abspath(sys.argv[2])) + b"\r", .9)
            frame = tui.send(F1 + b"lllllll\r")
            assert b"FX1" in frame and b"Master" in frame and b"AUX" in frame, frame[-5000:]
            frame = tui.send(b"\r")
            assert b"Load VST3 effect" in frame, (tui.process.poll(), frame[-8000:])
            frame = tui.send(b"\x15" + os.fsencode(os.path.abspath(sys.argv[3])) + b"\r", .9)
            assert b"Effect loaded" in frame, frame[-5000:]
            frame = tui.send(b"\r")
            assert b"VST3 editor:" in frame and b"Modulation" in frame, frame[-5000:]
            assert b"0.25" in tui.send(b"\r\x150.25\r")
            tui.send(b"\x1b")
            frame = tui.send(F1 + b"lllllll" + b"jjj\r")
            assert b"Set track send" in frame, frame[-5000:]
            assert b"whole number from 0 to 100" in tui.send(b"\x15101\r")
            tui.send(b"\x1550\r")
            tui.send(b"J")  # Return fader 100 -> 99.
            # Expand the source channel and edit its FX send, not its volume.
            frame = tui.send(b"hz")
            assert b"50%" in frame and b"WET" in frame, frame[-5000:]
            frame = tui.send(b"lK")
            assert b"51%" in frame, frame[-5000:]
            frame = tui.send(b"J")
            assert b"50%" in frame, frame[-5000:]
            tui.send(b"zl")  # Collapse, return to aux strip, preserve round-trip focus.
            tui.send(b"\x13")
            frame = tui.send(b"\x15" + os.fsencode(path) + b"\r", .6)
            assert b"Saved:" in frame, frame[-5000:]
        finally:
            tui.close()
        saved = path.read_bytes()
        assert b"AUX_EFFECTS" in saved
        tui = Terminal(str(path))
        try:
            frame = tui.read(1)
            assert b"FX1" in frame and b"99" in frame, frame[-5000:]
            tui.send(b"\x13", .6)
            assert path.read_bytes() == saved, "Aux session changed on round trip"
            assert b"0.25" in tui.send(b"\r")
            tui.send(b"\x1b")
            frame = tui.send(F1 + b"lllllll" + b"jjj\r")
            assert b"50" in frame and b"Set track send" in frame
            tui.send(b"\x1b")
            assert b"MIDI input adapter" in tui.send(F1 + b"jjjjj\r")
            tui.send(b"\rk\r")
            tui.send(b"\t\rk\r")
            tui.send(b"\t\rjj\r")  # Request 512 frames before applying.
            tui.send(b"\t\rjjj\r")  # Device default -> 96 kHz
            frame = tui.send(b"\t\r", .6)
            assert b"Settings applied. Audio disabled." in frame, frame[-5000:]
            assert b"0.25" in tui.send(b"\r")
            tui.send(b"\x1b")
            tui.send(b"\x13", .6)
            assert path.read_bytes() == saved, "Device replacement lost effect state"
            # Invalid replacement must retain the working effect.
            tui.send(F1 + b"lllllll" + b"j\r")
            frame = tui.send(b"\x15" + os.fsencode(os.path.abspath(sys.argv[2])) + b"\r", .9)
            assert b"not an instrument" in frame, frame[-5000:]
            assert b"0.25" in tui.send(b"\r")
            tui.send(b"\x1b")
            # Multiple aux channels occupy their own bank, not Pattern tracks.
            frame = tui.send(F1 + b"lllllll\r")
            assert b"FX1" in frame and b"FX2" in frame and b"Master" in frame
            frame = tui.send(b"h\r")
            assert b"0.25" in frame
        finally:
            tui.close()
    print("PASS: aux effect load/edit/send/return, session and device retention, invalid replacement")


if __name__ == "__main__":
    main()
