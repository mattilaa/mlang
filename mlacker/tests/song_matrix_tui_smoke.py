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
        # Ctrl+P plays the matrix from the cursor row; the status bar runs.
        frame = expect(tui.send(b"\x10", 0.9), b"Playing the matrix from row", b"PLAY")
        # In the matrix, Space stops and starts the matrix too: every lane of
        # the row plays, not just the pattern under the cursor.
        expect(tui.send(b" ", 0.9), b"Matrix stopped", b"STOP")
        expect(tui.send(b" ", 0.9), b"Playing the matrix from row", b"PLAY")
        # Ctrl+P toggles matrix playback too, in either spelling.
        expect(tui.send(b"\x1b[112;5u", 0.9), b"Matrix stopped")
        expect(tui.send(b"\x10", 0.9), b"Playing the matrix")
        expect(tui.send(b"\x10", 0.9), b"Matrix stopped")

        # The split renamed the track of the pattern that was placed, so open
        # that pattern in the editor to see it.
        expect(tui.send(b"M", 0.8), b"Song matrix closed")
        tui.send(F1 + b"ll\r", 0.6)              # View > Patterns
        tui.send(b"\x1b[104;6u", 0.5)            # focus the sidebar
        frame = tui.send(b"k", 0.8)              # select pattern 1
        assert b"- 2" in frame, frame[-4000:]

        # Browsing the matrix follows the pattern under the cursor: the editor
        # and the mixer show that pattern's tracks and routing, with nothing
        # playing.
        tui.send(b"\x1b[108;6u", 0.5)            # focus the pattern pane
        expect(tui.send(b"M", 0.8), b"Song matrix")
        tui.send(b"o", 0.6)                      # an empty row below, no clash
        tui.send(b"\r", 0.7)                     # pattern picker for the cell
        expect(tui.send(b"jj\r", 1.0), b"pattern 2")
        # Moving the cursor names the pattern the mixer and editor now show.
        expect(tui.send(b"k", 0.9), b"Pattern 1: Untitled")
        expect(tui.send(b"j", 0.9), b"Pattern 2: Untitled copy")
        # Pattern > Follow matrix patterns turns the whole behaviour off.
        expect(tui.send(F1 + b"llll" + b"j" * 4 + b"\r", 0.9), b"Follow matrix patterns: off")
        frame = tui.send(b"k", 0.9)
        assert b"Pattern 1: Untitled" not in frame, frame[-3000:]
        expect(tui.send(F1 + b"llll" + b"j" * 4 + b"\r", 0.9), b"Follow matrix patterns: on")

        # A pattern longer than one matrix row takes as many rows as it needs,
        # and the lane beside it keeps its own shorter patterns.
        tui.send(b"k" * 4 + b"h" * 4, 0.9)       # row 1, lane 1: it holds pattern 1
        tui.send(b"M", 0.7)                      # editor, on that pattern
        expect(tui.send(F1 + b"llll" + b"jjj\r", 0.8), b"Pattern length")
        tui.send(b"\x15128\r", 0.9)
        frame = expect(tui.send(b"M", 0.8), b"Song matrix")
        assert "\u252c".encode() in frame, frame[-3000:]
        # Its second row belongs to it: nothing else can start there.
        expect(tui.send(b"j\r", 0.9), b"plays pattern")

        # Looping: a pattern placed on a new row repeats down its lane as a
        # darker copy until a split, which shows where the loop ends.
        tui.send(b"o", 0.6)                      # row 3, below the long pattern
        expect(tui.send(b"\r", 0.7), b"Choose pattern")
        expect(tui.send(b"jj\r", 1.0), b"pattern 2")
        tui.send(b"o", 0.6)                      # row 4
        tui.send(b"o", 0.6)                      # row 5
        tui.send(b"kk", 0.6)                     # back to row 3
        frame = expect(tui.send(b"r", 0.9), b"loops from row 3")
        assert "Untitl \u252c \u00bb".encode() in frame, frame[-3000:]
        # The pattern spans rows 3-4, so its first repeat starts on row 5.
        frame = expect(tui.send(b"jj", 0.8), "\u00bb Untitl".encode())
        expect(tui.send(b"s", 0.9), b"Split at row 5", "\u2573 end".encode())
        # Backspace on the split lets the loop run on; on a repeat it splits.
        expect(tui.send(b"\x7f", 0.9), b"Split removed at row 5")
        expect(tui.send(b"\x7f", 0.9), b"Loop ends at row 5")
        # r on the pattern switches its loop off again.
        expect(tui.send(b"kk", 0.6), b"Song matrix")
        expect(tui.send(b"r", 0.9), b"no longer loops")
        expect(tui.send(b"M", 0.8), b"Song matrix closed")
    finally:
        tui.close()


if __name__ == "__main__":
    main()
