"""Mla Drum sampler pads from the UI: piano key picker, override/clear, destructive
sample editing, kit presets and session round trip.

Usage: sampler_pads_tui_smoke.py <mlacker> <MlaDrum.vst3>. No audio hardware.
"""
import struct
import sys
import tempfile
import wave
from pathlib import Path

from session_tui_smoke import Terminal

# Instrument menu (8th menu): items 4/5 presets, 6/7 pad actions.
SAVE_PRESET = b"\tlllllll" + b"j" * 4 + b"\r"
LOAD_PRESET = b"\tlllllll" + b"j" * 5 + b"\r"
SEND_TO_PAD = b"\tlllllll" + b"j" * 6 + b"\r"
PAD_FROM_FILE = b"\tlllllll" + b"j" * 7 + b"\r"
# Audio menu (6th menu): item 8 is the destructive sample editor.
EDIT_SAMPLE = b"\tlllll" + b"j" * 8 + b"\r"
BACKSPACE = b"\x7f"


def write_wav(path, value):
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(48000)
        out.writeframes(struct.pack("<h", value) * 2400)


def expect(frame, *needles):
    for needle in needles:
        assert needle in frame, (needle, frame[-4000:])
    return frame


def main():
    with tempfile.TemporaryDirectory(prefix="mlacker-pads-") as directory:
        root = Path(directory)
        kick, snare = root / "kick.wav", root / "snare.wav"
        write_wav(kick, 16000)
        write_wav(snare, -12000)
        path = root / "pads.mlack"
        other = root / "other.mlack"
        kit = root / "Mla Drum - Kit.mlapre"
        tui = Terminal(cwd=directory)
        try:
            tui.read(0.8)
            expect(tui.send(b"\tllljljj\r"), b"Instrument track created")
            expect(tui.send(b"\tlllllljjj\r"), b"Add VST3 instrument")
            expect(tui.send(b"\x15" + bytes(Path(sys.argv[2]).resolve()) + b"\r", 0.9), b"Instrument loaded: Mla Drum")

            # From disk: the piano opens on the first empty pad (Root Key C-2 = 36).
            expect(tui.send(PAD_FROM_FILE), b"Load drum key from file", b"C-2 (MIDI 36) pad 1: empty")
            tui.send(b"ll")
            expect(tui.send(b"\r"), b"Load sample for pad 3")
            expect(tui.send(b"\x15" + bytes(kick) + b"\r", 0.7), b"Pad 3: kick.wav (added to Audio)")

            # From the Audio list (the newly added snare is selected).
            expect(tui.send(b"\tllllll\r"), b"Add audio")
            tui.send(b"\x15" + bytes(snare) + b"\r", 0.7)
            expect(tui.send(SEND_TO_PAD), b"Send sample to drum key", b"pad 1: empty")
            tui.send(b"l")
            expect(tui.send(b"\r", 0.5), b"Pad 2: snare.wav")

            # Override a loaded pad, then clear another one.
            tui.send(SEND_TO_PAD)
            expect(tui.send(b"ll"), b"kick.wav", b"Enter replaces")
            expect(tui.send(b"\r", 0.5), b"Pad 3: snare.wav")
            tui.send(SEND_TO_PAD)
            tui.send(b"l")
            expect(tui.send(BACKSPACE, 0.5), b"Pad 2 cleared")

            # Pad 1 from a second copy of the kick (sample 3 in the Audio list).
            tui.send(PAD_FROM_FILE)
            expect(tui.send(b"\r"), b"Load sample for pad 1")
            expect(tui.send(b"\x15" + bytes(kick) + b"\r", 0.7), b"Pad 1: kick.wav (added to Audio)")

            # Destructive edit: discarded edits change nothing...
            expect(tui.send(EDIT_SAMPLE, 0.5), b"Edit sample 3: kick.wav")
            expect(tui.send(b"r"), b"[modified]")
            expect(tui.send(b"\x1b", 0.6), b"Sample edits discarded")
            # ...saved edits replace the sample and refresh the pad that uses it.
            tui.send(EDIT_SAMPLE, 0.5)
            expect(tui.send(b"n"), b"Normalized to 0 dBFS")
            expect(tui.send(b"u"), b"Undone")
            tui.send(b"n")
            expect(tui.send(b"\r", 0.6), b"Sample 3 saved: 0 placement(s), 1 drum pad(s) updated")

            # Kit preset: parameters plus embedded pads (MLAPRE 1.1).
            expect(tui.send(SAVE_PRESET), b"Save plugin preset (.mlapre)", b"Mla Drum - ")
            expect(tui.send(b"Kit\r", 0.8), b"Saved plugin preset:")
            data = kit.read_bytes()
            assert data.startswith(struct.pack("<q", 6) + b"MLAPRE" + struct.pack("<qq", 1, 1)), data[:40]
            assert b"SAMPLER_PADS" in data

            # Clear pad 1, then loading the kit restores it.
            tui.send(SEND_TO_PAD)
            expect(tui.send(b"h"), b"Enter replaces")
            expect(tui.send(BACKSPACE, 0.5), b"Pad 1 cleared")
            expect(tui.send(LOAD_PRESET), b"Load plugin preset (.mlapre)")
            expect(tui.send(b"\x15" + bytes(kit) + b"\r", 1.2), b"Loaded kit preset: 2 pad(s)")
            expect(tui.send(SEND_TO_PAD), b"pad 2: empty")  # pad 1 is loaded again
            expect(tui.send(b"h"), b"kick.wav")
            tui.send(b"\x1b", 0.6)

            # Device changes rebuild every instrument; pads must be re-sent.
            expect(tui.send(b"\tjjjj\r"), b"Master output (AUHAL)")  # File > Settings
            tui.send(b"\rk\r")
            tui.send(b"\t\rk\r")
            frame = expect(tui.send(b"\t\r", 0.8), b"Settings applied")
            assert b"sampler pads" not in frame, frame[-4000:]

            # A cancelled pad file dialog must not capture the next file dialog.
            tui.send(PAD_FROM_FILE)
            expect(tui.send(b"\r"), b"Load sample for pad 2")
            tui.send(b"\x1b", 0.6)
            expect(tui.send(b"\tjjjjjj\r"), b"Save session (.mlack)")
            frame = expect(tui.send(b"\x15" + bytes(path) + b"\r", 0.7), b"Saved:")
            assert b"Pad 2" not in frame, frame[-4000:]
        finally:
            tui.close()

        saved = path.read_bytes()
        # Ascending (slot, pad): pad index 0 <- sample 2 (edited kick),
        # pad index 2 <- sample 1 (snare, the override).
        pads = struct.pack("<q", 12) + b"SAMPLER_PADS" + struct.pack("<7q", 2, 1, 0, 2, 1, 2, 1)
        assert saved.endswith(pads), saved[-120:]
        # The edited kick is stored normalized (16000/32768 scaled to 1.0).
        assert struct.pack("<d", 1.0) * 16 in saved

        tui = Terminal(str(path), cwd=directory)
        try:
            frame = tui.read(1.2)
            assert b"Opened:" in frame and b"sampler pad" not in frame, frame[-5000:]
            tui.send(b"\x13", 0.6)
            assert path.read_bytes() == saved, "Reopened pad session changed on save"
        finally:
            tui.close()

        # A pad on a slot without a loaded plugin is rejected.
        other.write_bytes(saved[:-48] + struct.pack("<6q", 1, 0, 2, 7, 2, 1))
        tui = Terminal(str(other), cwd=directory)
        try:
            frame = tui.read(1.2)
            assert b"Opened:" not in frame and b"Invalid" in frame, frame[-5000:]
        finally:
            tui.close()


if __name__ == "__main__":
    main()
