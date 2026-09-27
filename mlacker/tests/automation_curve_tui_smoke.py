"""Automation curves: Ctrl+V marks two CC values, Ctrl+A opens the curve dialog
(Linear by default, OK/Cancel) and fills the 1/64 steps between them.

Usage: automation_curve_tui_smoke.py <mlacker>. No audio or MIDI hardware.
"""
from session_tui_smoke import Terminal

F1 = b"\x1bOP"
CTRL_V = b"\x16"
CTRL_A = b"\x01"


def expect(frame, *needles):
    for needle in needles:
        assert needle in frame, (needle, frame[-4000:])
    return frame


def main():
    tui = Terminal()
    try:
        tui.read(0.8)
        expect(tui.send(F1 + b"lll\r", 0.6), b"Track 1")         # Track > Create MIDI track
        tui.send(b"\x1b[108;6u", 0.4)                             # focus the pattern editor
        expect(tui.send(b"zz", 0.5), b"CC1")                      # show LEN/OFF, then CC
        tui.send(b"llll", 0.4)                                    # NOTE -> CC1
        expect(tui.send(b"\r\x150\r", 0.5), b"0")
        tui.send(b"jj", 0.4)
        expect(tui.send(b"\r\x1580\r", 0.5), b"80")
        # Ctrl+A needs two marks; an empty cell cannot be marked.
        expect(tui.send(CTRL_A, 0.5), b"Mark two CC values with Ctrl+V first")
        tui.send(b"k", 0.3)
        expect(tui.send(CTRL_V, 0.5), b"Mark a CC value, not an empty step")
        tui.send(b"j", 0.3)
        expect(tui.send(CTRL_V, 0.5), b"CC value marked")
        tui.send(b"kk", 0.3)
        expect(tui.send(CTRL_V, 0.5), b"Two CC values marked")
        # Cancel leaves the pattern alone and keeps the marks.
        expect(tui.send(CTRL_A, 0.6), b"Automation curve", b"Linear", b"Exponential", b"OK", b"Cancel")
        expect(tui.send(b"\x1b", 0.6), b"Cancelled.")
        # OK on the default Linear ramp writes the seven steps between.
        expect(tui.send(CTRL_A, 0.6), b"Automation curve")
        frame = expect(tui.send(b"\r", 0.8), b"Linear curve: 7 CC step(s) written")
        # Unzoomed, a cell holding 1/64 steps shows its first value and "+".
        expect(frame, b"0+", b"40+")
    finally:
        tui.close()
    print("PASS: automation curve marks, dialog and 1/64 ramp")


if __name__ == "__main__":
    main()
