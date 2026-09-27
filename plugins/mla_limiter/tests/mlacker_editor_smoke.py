#!/usr/bin/env python3
"""Load the bundle, edit its VST3 controls, and reopen a saved mlacker session.
Usage: mlacker_editor_smoke.py <mlacker> <MlaLimiter.vst3>
"""
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "mlacker/tests"))
from session_tui_smoke import Terminal, F1


def expect(frame, *labels):
    for label in labels:
        assert label in frame, (label, frame[-6000:])


def main():
    bundle = os.fsencode(Path(sys.argv[2]).resolve())
    with tempfile.TemporaryDirectory(prefix="mla-limiter-ui-") as directory:
        session = Path(directory) / "limiter.mlack"
        tui = Terminal()
        try:
            tui.read(.8)
            expect(tui.send(F1 + b"lllllll\r"), b"FX1")
            expect(tui.send(b"\r"), b"Load VST3 effect")
            expect(tui.send(b"\x15" + bundle + b"\r", 1), b"Effect loaded")
            expect(tui.send(b"\r"), b"VST3 editor:", b"Mode", b"Threshold", b"Ceiling", b"Release")
            # mlacker represents continuous VST3 controls in normalized 0..1.
            expect(tui.send(b"j\r\x150.75\r"), b"0.75")
            # Scroll through the controls, down to Punch, Fat and Bypass.
            expect(tui.send(b"j" * 7), b"Punch", b"Fat", b"Bypass")
            tui.send(b"\x1b")
            tui.send(b"\x13")
            expect(tui.send(b"\x15" + os.fsencode(session) + b"\r", .7), b"Saved:")
        finally:
            tui.close()
        tui = Terminal(str(session))
        try:
            expect(tui.read(1), b"Opened:")
            expect(tui.send(F1 + b"llllllljj\r"), b"VST3 editor:", b"Threshold", b"0.75")
        finally:
            tui.close()
    print("Mla Limiter mlacker editor smoke passed")


if __name__ == "__main__":
    main()
