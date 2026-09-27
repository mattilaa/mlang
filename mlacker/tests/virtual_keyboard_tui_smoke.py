"""Virtual keyboard pane: Shift+P / View menu toggle, octave keys, notes step
entered into an armed MIDI track, and Space still driving the transport."""
from session_tui_smoke import Terminal, F1

VIEW = F1 + b"ll"
KEYBOARD_ITEM = VIEW + b"j" * 8


def expect(frame, *needles):
    for needle in needles:
        assert needle in frame, (needle, frame[-4000:])
    return frame


def main():
    tui = Terminal()
    try:
        tui.read(.8)
        expect(tui.send(b"P"), b"Keyboard | Octave 3 (C-3..E-5)", b"C-4", b"Shift+P hide")
        # No track yet: keys only explain why nothing plays.
        expect(tui.send(b"z"), b"select a MIDI or Instrument track")
        expect(tui.send(b"P"), b"Virtual keyboard closed")
        expect(tui.send(VIEW), b"[ ] Show virtual keyboard")
        tui.send(b"\x1b")
        expect(tui.send(KEYBOARD_ITEM + b"\r"), b"Keyboard | Octave 3")
        expect(tui.send(VIEW), b"[x] Show virtual keyboard")
        tui.send(b"\x1b")
        expect(tui.send(b"P"), b"Virtual keyboard closed")
        # A MIDI track armed in the mixer (Tab from the sidebar), then played
        # from the keyboard: the note is step entered at the cursor.
        tui.send(b"\x1b[Z\x1b[Z")
        expect(tui.send(F1 + b"lll\r"), b"Track 1")
        tui.send(b"m\t\tR")
        tui.send(b"m")
        expect(tui.send(b"P"), b"Keyboard | Octave 3 (C-3..E-5) | Track 1")
        expect(tui.send(b"="), b"Octave 4 (C-4..E-6)")
        expect(tui.send(b"-"), b"Octave 3 (C-3..E-5)")
        expect(tui.send(b"s", .8), b"C#3")
        # Kitty key releases end notes and never act as key presses.
        tui.send(b"\x1b[115;1:3u\x1b[113;1:3u\x1b[112;1:3u")
        assert tui.process.poll() is None
        # Space plays and stops from the keyboard pane; q plays instead of quitting.
        # The armed track makes Space count in and record.
        expect(tui.send(b" "), b"COUNT IN")
        tui.send(b" ")
        expect(tui.send(b"\r"), b"STOP")  # Accept the recorded track name.
        tui.send(b"q")
        assert tui.process.poll() is None
    finally:
        # Ctrl+C quits from any pane; q is a note while the keyboard has focus.
        tui.send(b"\x03")
        tui.close()


if __name__ == "__main__":
    main()
