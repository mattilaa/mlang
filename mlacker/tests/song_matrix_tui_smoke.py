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

        # Shift+M shows the matrix in the pattern pane.
        frame = expect(tui.send(b"M", 0.6), b"Song matrix", b"ROW", b"L1")
        assert b"Enter places the pattern" in frame, frame[-4000:]
        # Select pattern 1 in the sidebar (loading a plugin left it on the
        # Instruments list), then place it beside the clone.
        tui.send(F1 + b"ll\r")                   # View > Patterns
        tui.send(b"\x1b[104;6u")                 # focus the sidebar
        tui.send(b"k")                           # pattern 1
        tui.send(b"\x1b[108;6u")                 # back to the matrix
        tui.send(b"l")                           # lane 2
        frame = expect(tui.send(b"\r", 0.8), b"clash", b"Split")
        assert b"OK" in frame and b"Cancel" in frame, frame[-4000:]
        # Cancel leaves the cell alone.
        expect(tui.send(b"l\r", 0.8), b"Cancelled")
        frame = tui.send(b"\r", 0.8)             # ask again
        expect(frame, b"clash")
        # Split places the pattern and gives its track its own note line.
        frame = expect(tui.send(b"ll\r", 1.0), b"split onto their own note line")
        assert b"lane 2" in frame, frame[-4000:]
        # Closing the matrix shows the renamed track in the pattern editor.
        frame = expect(tui.send(b"M", 0.6), b"Song matrix closed")
        assert b"- 2" in tui.send(b"\x1b[104;6u", 0.5) + frame, frame[-4000:]
    finally:
        tui.close()


if __name__ == "__main__":
    main()
