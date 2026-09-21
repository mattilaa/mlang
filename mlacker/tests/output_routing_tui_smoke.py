"""Mixer output channels: the OUT row, Track > Set output channel, and saving.

Usage: output_routing_tui_smoke.py <mlacker> <instrument.vst3>. No audio hardware.
"""
import os
import sys
import tempfile
from pathlib import Path

from session_tui_smoke import Terminal

F1 = b"\x1bOP"
# Track menu: Set output channel is the seventh entry.
SET_OUTPUT = F1 + b"lll" + b"j" * 6 + b"\r"


def expect(frame, *needles):
    for needle in needles:
        assert needle in frame, (needle, frame[-4000:])
    return frame


def main():
    instrument = os.fsencode(os.path.abspath(sys.argv[2]))
    with tempfile.TemporaryDirectory(prefix="mlacker-outputs-") as folder:
        path = Path(folder) / "outputs.mlack"
        tui = Terminal()
        try:
            tui.read(0.8)
            expect(tui.send(F1 + b"lll" + b"j\r"), b"Audio 1 [AUDIO]")
            expect(tui.send(F1 + b"lll" + b"\r"), b"Track 2")
            # Every channel starts on master, below the fader value.
            expect(tui.send(b"m", 0.6), b"Mixer", b"OUT:MST")
            tui.send(b"m", 0.5)
            # A MIDI track picks the instrument its notes play.
            expect(tui.send(SET_OUTPUT, 0.8), b"No instrument loaded yet")
            expect(tui.send(F1 + b"llllll\r"), b"Add VST3 instrument")
            expect(tui.send(b"\x15" + instrument + b"\r", 1.0), b"Instrument loaded:")
            expect(tui.send(SET_OUTPUT, 0.8), b"MIDI output", b"MST", b"I1")
            expect(tui.send(b"j\r", 0.8), b"MIDI output: Mlacker Test Instrument #1")
            expect(tui.send(b"m", 0.6), b"OUT:I1")
            tui.send(b"m", 0.5)
            # Now that it plays an instrument, the same entry routes its audio.
            expect(tui.send(SET_OUTPUT, 0.8), b"Output channel", b"A1")
            expect(tui.send(b"j\r", 0.8), b"Output channel: A1")
            expect(tui.send(b"m", 0.6), b"I1>A1")
            tui.send(b"m", 0.5)
            assert b"Saved:" in tui.send(b"\x13" + b"\x15" + os.fsencode(path) + b"\r", 0.8)
        finally:
            tui.close()

        saved = path.read_bytes()
        assert b"TRACK_OUTPUTS" in saved
        tui = Terminal(str(path))
        try:
            expect(tui.read(1.0), b"Opened:")
            expect(tui.send(b"m", 0.6), b"I1>A1", b"OUT:MST")
            tui.send(b"m", 0.5)
            # Re-saving an unchanged session rewrites the same bytes.
            tui.send(b"\x13", 0.8)
            assert path.read_bytes() == saved
            # The picker opens on the current channel; k steps back to master.
            expect(tui.send(SET_OUTPUT, 0.8), b"Output channel")
            expect(tui.send(b"k\r", 0.8), b"Output channel: master")
            expect(tui.send(b"m", 0.6), b"OUT:I1")
            tui.send(b"q", 0.5)
        finally:
            tui.close()
    print("PASS: output channel labels, picker, routing and session round trip")


if __name__ == "__main__":
    main()
