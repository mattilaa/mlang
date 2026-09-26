"""Song matrix recording: Shift+R arms a pattern, Ctrl+P records into it while
the matrix plays, the pattern grows past its rows, and Ctrl+P ends it on a bar.

Usage: matrix_record_tui_smoke.py <mlacker>. No audio or MIDI hardware.
"""
import re

from session_tui_smoke import Terminal

F1 = b"\x1bOP"


def expect(frame, *needles):
    for needle in needles:
        assert needle in frame, (needle, frame[-4000:])
    return frame


def main():
    tui = Terminal()
    try:
        tui.read(0.8)
        expect(tui.send(F1 + b"lll\r", 0.6), b"Track 1")        # Track > Create MIDI track
        expect(tui.send(b"\x02", 0.5), b"Set BPM")                # 64 rows in 2.4 s
        expect(tui.send(b"\x15400\r", 0.5), b"BPM 400")
        expect(tui.send(b"M", 0.6), b"Song matrix")
        expect(tui.send(b"\r", 0.7), b"Choose pattern")
        expect(tui.send(b"j\r", 0.8), b"Row 1 lane 1: pattern 1")
        # An empty cell cannot be armed; the pattern's cell can, and again disarms.
        tui.send(b"l", 0.3)
        expect(tui.send(b"R", 0.5), b"Arm a cell that holds a pattern")
        tui.send(b"h", 0.3)
        expect(tui.send(b"R", 0.5), b"Pattern 1 armed")
        expect(tui.send(b"R", 0.5), b"Recording disarmed")
        expect(tui.send(b"R", 0.5), b"Pattern 1 armed")
        # Ctrl+P plays the matrix and records; the take outlasts the 64 rows.
        expect(tui.send(b"\x10", 0.8), b"Recording into pattern 1", b"REC")
        tui.read(3.4)
        # Ctrl+P stops; as after any take, the track can be named.
        expect(tui.send(b"\x10", 0.8), b"STOP", b"Rename track")
        frame = tui.send(b"\x1b", 0.6)                            # keep the track name
        rows = int(re.search(rb"Recorded pattern 1: (\d+) rows", frame)[1])
        # Stopped past 64 rows, on a 16-row bar line.
        assert rows > 64 and rows % 16 == 0, rows
        # The take disarmed the pattern: Ctrl+P now only plays.
        expect(tui.send(b"\x10", 0.8), b"Playing the matrix from row 1")
        expect(tui.send(b"\x10", 0.8), b"Matrix stopped")
    finally:
        tui.close()
    print("PASS: matrix recording arms a pattern, grows it and ends it on a bar")


if __name__ == "__main__":
    main()
