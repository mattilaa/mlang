#!/usr/bin/env python3
"""Spectrum analyzer / master bus: toggle, controls, session round trip.
Usage: spectrum_tui_smoke.py <mlacker>
"""
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from session_tui_smoke import Terminal, F1

CTRL_SHIFT_M = b"\x1b[109;6u"  # kitty keyboard protocol, which mlacker enables
UP = b"\x1b[A"
RIGHT = b"\x1b[C"


def expect(frame, *labels):
    for label in labels:
        assert label in frame, (label, frame[-6000:])


def main():
    with tempfile.TemporaryDirectory(prefix="mlacker-spectrum-") as directory:
        session = Path(directory) / "spectrum.mlack"
        tui = Terminal()
        try:
            tui.read(0.8)
            frame = tui.send(CTRL_SHIFT_M, 0.6)
            # Frequency band under the graph, EQ strips and the master strip.
            expect(frame, b"Spectrum / Master", b"100Hz", b"1kHz", b"HP", b"EQ1", b"EQ4", b"Master")
            # Defaults: 20 Hz high-pass, bands at 100/500/2.5k/8k Hz, Q 1, unity volume.
            expect(frame, b"20 ", b"100 ", b"500 ", b"2.5k", b"8.0k", b"Q1.0", b"100%")
            # Focus the analyzer pane; EQ1 gain up two half-dB steps.
            frame = tui.send(b"\t\t" + UP + UP, 0.5)
            expect(frame, b"+1.0")
            # Master volume: four strips to the right, one step down.
            frame = tui.send(RIGHT * 4 + b"j", 0.5)
            expect(frame, b"99%")
            tui.send(b"\x13")
            expect(tui.send(b"\x15" + os.fsencode(session) + b"\r", 0.7), b"Saved:")
        finally:
            tui.close()
        tui = Terminal(str(session))
        try:
            frame = tui.read(1.0)
            # The analyzer is still open with the saved bus.
            expect(frame, b"Opened:", b"Spectrum / Master", b"+1.0", b"99%")
            # View > Spectrum analyzer > Change details.
            expect(tui.send(F1 + b"ll" + b"j" * 7 + RIGHT + b"\r", 0.5), b"Spectrum detail: Braille (fine)")
            # View > Show spectrum analyzer closes it again.
            frame = tui.send(F1 + b"ll" + b"j" * 6 + b"\r", 0.5)
            assert b"Spectrum / Master" not in frame, frame[-6000:]
        finally:
            tui.close()
    print("mlacker spectrum analyzer smoke passed")


if __name__ == "__main__":
    main()
