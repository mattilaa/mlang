"""Ctrl+P previews the highlighted file in the Add audio dialog, and the
selected sample in View > Audio: again stops it, and moving to another file or
sample stops it too.

Usage: audio_preview_tui_smoke.py <mlacker> <instrument.vst3>. No audio
hardware: loading the instrument creates the offline output the preview
plays on, where it never reaches its end."""
import os
import struct
import sys
import tempfile
import wave
from pathlib import Path

from session_tui_smoke import Terminal, F1

ADD_AUDIO = F1 + b"lllll\r"
FILES_PANE = b"\x1b[108;6u"
HINT = b"Ctrl+P: play/stop preview"
PLAYING = b"Playing preview | Ctrl+P stops"


def write_wav(path, frames):
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(48000)
        out.writeframes(struct.pack("<h", 8000) * frames)


def expect(frame, *needles):
    for needle in needles:
        assert needle in frame, (needle, frame[-4000:])
    return frame


def main():
    with tempfile.TemporaryDirectory(prefix="mlacker-preview-") as directory:
        root = Path(directory)
        write_wav(root / "a_long.wav", 48000 * 30)
        write_wav(root / "b_short.wav", 4800)
        tui = Terminal(cwd=directory)
        try:
            tui.read(0.8)
            # Without any output there is nothing to play on.
            tui.send(ADD_AUDIO)
            tui.send(FILES_PANE)
            expect(tui.send(b"\x10"), b"No audio output to preview on")
            tui.send(b"\x1b", 0.6)
            expect(tui.send(F1 + b"lll" + b"jj\r"), b"Instrument track created")
            expect(tui.send(F1 + b"llllll\r"), b"Add VST3 instrument")
            expect(tui.send(b"\x15" + os.fsencode(os.path.abspath(sys.argv[2])) + b"\r", 0.9), b"Instrument loaded:")
            expect(tui.send(ADD_AUDIO), b"Add audio", b"a_long.wav", HINT)
            tui.send(FILES_PANE)
            expect(tui.send(b"\x10"), PLAYING)
            # Ctrl+P again stops; a third press plays it again.
            expect(tui.send(b"\x10"), HINT)
            expect(tui.send(b"\x1b[112;5u"), PLAYING)
            # Moving to another file stops the preview.
            expect(tui.send(b"j"), HINT)
            expect(tui.send(b"\x10"), PLAYING)
            # Mark both files and add them; closing the dialog ends the preview.
            tui.send(b"k")
            tui.send(b"  ")
            expect(tui.send(b"\r", 0.8), b"Added 2 samples")
            # View > Audio: Ctrl+P plays the selected sample, again stops it,
            # and moving the selection stops it too.
            expect(tui.send(F1 + b"ll" + b"jj\r"), b" Audio ")
            tui.send(b"\x1b[104;6u")  # focus the sidebar
            tui.send(b"gg")
            expect(tui.send(b"\x10"), b"Playing sample 1: a_long.wav | Ctrl+P stops")
            expect(tui.send(b"\x10"), b"Preview stopped")
            expect(tui.send(b"\x1b[112;5u"), b"Playing sample 1: a_long.wav")
            expect(tui.send(b"j"), b"Preview stopped")
            expect(tui.send(b"\x10"), b"Playing sample 2: b_short.wav")
            # Leaving the sidebar stops it as well.
            expect(tui.send(b"\t"), b"Preview stopped")
        finally:
            tui.close()


if __name__ == "__main__":
    main()
