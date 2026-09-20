"""Song matrix: parallel lanes, the clash warning and its Split resolution.

Usage: song_matrix_tui_smoke.py <mlacker> <instrument.vst3>. No audio hardware.
"""
import os
import sys

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
        # An instrument track playing a loaded plugin, with one note in it.
        expect(tui.send(F1 + b"lll" + b"jj\r"), b"Instrument track created")
        expect(tui.send(F1 + b"llllll\r"), b"Add VST3 instrument")
        expect(tui.send(b"\x15" + os.fsencode(os.path.abspath(sys.argv[2])) + b"\r", 1.0),
               b"Instrument loaded:")
        tui.send(b"\x1b[108;6u")                 # focus the pattern editor
        expect(tui.send(b"K", 0.5), b"C-4")      # default note in the NOTE cell
        expect(tui.send(F1 + b"llll" + b"j\r", 0.6), b"Pattern / 2")  # Pattern > Clone

        # Shift+M shows the matrix in place of the pattern editor.
        frame = expect(tui.send(b"M", 0.6), b"Song matrix", b"ROW", b"L1", b"001")
        assert b"NOTE" not in frame, "pattern editor still drawn under the matrix"
        # Menus draw above it.
        frame = expect(tui.send(F1 + b"llll", 0.6), b"Add pattern", b"Clone pattern")
        tui.send(b"\x1b", 0.4)
        # Enter opens the pattern picker for the cell.
        tui.send(b"l")                           # lane 2
        frame = expect(tui.send(b"\r", 0.7), b"Choose pattern", b"001:", b"002:")
        assert b"(empty)" in frame, frame[-4000:]
        # Pick pattern 1: same instrument and note as the clone beside it.
        tui.send(b"j", 0.4)
        frame = expect(tui.send(b"\r", 1.0), b"clash", b"Split")
        assert b"OK" in frame and b"Cancel" in frame, frame[-4000:]
        # Cancel leaves the cell alone.
        expect(tui.send(b"l\r", 0.8), b"Cancelled")
        # Choose it again through the picker, and split this time.
        tui.send(b"\r", 0.6)                     # picker
        tui.send(b"j", 0.4)                      # 001:
        expect(tui.send(b"\r", 1.0), b"clash")
        frame = expect(tui.send(b"ll\r", 1.2), b"split onto their own note line")
        assert b"lane 2" in frame, frame[-4000:]
        # The split renamed the track of the pattern that was placed, so open
        # that pattern in the editor to see it.
        expect(tui.send(b"M", 0.8), b"Song matrix closed")
        tui.send(F1 + b"ll\r", 0.6)              # View > Patterns
        tui.send(b"\x1b[104;6u", 0.5)            # focus the sidebar
        frame = tui.send(b"k", 0.8)              # select pattern 1
        assert b"- 2" in frame, frame[-4000:]
    finally:
        tui.close()


if __name__ == "__main__":
    main()
