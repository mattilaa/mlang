"""Mla Drum sampler pads from the UI: file and Audio-list sources, session round trip.

Usage: sampler_pads_tui_smoke.py <mlacker> <MlaDrum.vst3>. No audio hardware.
"""
import struct
import sys
import tempfile
import wave
from pathlib import Path

from session_tui_smoke import Terminal

# Instrument menu (8th menu): items 6/7 are the pad actions.
SEND_TO_PAD = b"\tlllllll" + b"j" * 6 + b"\r"
PAD_FROM_FILE = b"\tlllllll" + b"j" * 7 + b"\r"


def write_wav(path, value):
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(48000)
        out.writeframes(struct.pack("<h", value) * 2400)


def main():
    with tempfile.TemporaryDirectory(prefix="mlacker-pads-") as directory:
        root = Path(directory)
        kick, snare = root / "kick.wav", root / "snare.wav"
        write_wav(kick, 16000)
        write_wav(snare, -12000)
        path = root / "pads.mlack"
        other = root / "other.mlack"
        tui = Terminal()
        try:
            tui.read(0.8)
            assert b"Instrument track created" in tui.send(b"\tllljljj\r")
            assert b"Add VST3 instrument" in tui.send(b"\tlllllljjj\r")
            frame = tui.send(b"\x15" + bytes(Path(sys.argv[2]).resolve()) + b"\r", 0.9)
            assert b"Instrument loaded: Mla Drum" in frame, frame[-4000:]

            # From disk: pad number, then a WAV that joins the Audio list.
            assert b"Load pad sample from file" in tui.send(PAD_FROM_FILE)
            assert b"from 1 to 16" in tui.send(b"\x1517\r"), "pad 17 accepted"
            assert b"Load sample for pad 3" in tui.send(b"\x153\r")
            frame = tui.send(b"\x15" + bytes(kick) + b"\r", 0.7)
            assert b"Pad 3: kick.wav (added to Audio)" in frame, frame[-4000:]

            # From the session's Audio list: the newly added sample is selected.
            assert b"Add audio" in tui.send(b"\tllllll\r")  # Add > Audio
            frame = tui.send(b"\x15" + bytes(snare) + b"\r", 0.7)
            assert b"Send sample to pad" in tui.send(SEND_TO_PAD)
            frame = tui.send(b"\x152\r", 0.5)
            assert b"Pad 2: snare.wav" in frame, frame[-4000:]

            # Device changes rebuild every instrument; pads must be re-sent.
            assert b"Master output (AUHAL)" in tui.send(b"\tjjjj\r")  # File > Settings
            tui.send(b"\rk\r")
            tui.send(b"\t\rk\r")
            frame = tui.send(b"\t\r", 0.8)
            assert b"Settings applied" in frame and b"sampler pads" not in frame, frame[-4000:]

            # A cancelled pad file dialog must not capture the next file dialog.
            tui.send(PAD_FROM_FILE)
            assert b"Load sample for pad 5" in tui.send(b"\x155\r")
            tui.send(b"\x1b", 0.6)
            assert b"Save session (.mlack)" in tui.send(b"\tjjjjjj\r")
            frame = tui.send(b"\x15" + bytes(path) + b"\r", 0.7)
            assert b"Saved:" in frame and b"Pad 5" not in frame, frame[-4000:]
        finally:
            tui.close()

        saved = path.read_bytes()
        # Ascending (slot, pad): slot 1 pad index 1 <- sample 1 (snare),
        # slot 1 pad index 2 <- sample 0 (kick).
        pads = struct.pack("<q", 12) + b"SAMPLER_PADS" + struct.pack("<7q", 2, 1, 1, 1, 1, 2, 0)
        assert saved.endswith(pads), saved[-120:]

        tui = Terminal(str(path), cwd=directory)
        try:
            frame = tui.read(1.2)
            assert b"Opened:" in frame and b"sampler pad" not in frame, frame[-5000:]
            tui.send(b"\x13", 0.6)
            assert path.read_bytes() == saved, "Reopened pad session changed on save"
        finally:
            tui.close()

        # A pad on a slot without a loaded plugin is rejected.
        other.write_bytes(saved[:-24] + struct.pack("<3q", 7, 2, 0))
        tui = Terminal(str(other), cwd=directory)
        try:
            frame = tui.read(1.2)
            assert b"Opened:" not in frame and b"Invalid" in frame, frame[-5000:]
        finally:
            tui.close()


if __name__ == "__main__":
    main()
