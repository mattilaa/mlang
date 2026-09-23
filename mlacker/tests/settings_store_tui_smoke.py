"""The Mlacker folder: preset layout, .mlacker.conf, and missing devices.

Usage: settings_store_tui_smoke.py <mlacker>. No audio hardware; HOME is moved
to a temporary directory so the real ~/Documents/Mlacker is never touched.
"""
import os
import sys
import tempfile
from pathlib import Path

F1 = b"\x1bOP"
# Settings dialog: MIDI, output, buffer, rate, folder, buttons.
TO_BUTTONS = b"\t" * 5


def expect(frame, *needles):
    for needle in needles:
        assert needle in frame, (needle, frame[-4000:])
    return frame


def main():
    with tempfile.TemporaryDirectory(prefix="mlacker-home-") as home:
        # MLACKER_HOME keeps presets and settings out of the real ~/Documents.
        root = Path(home) / "Mlacker"
        os.environ["MLACKER_HOME"] = str(root)
        from session_tui_smoke import Terminal
        tui = Terminal()
        try:
            tui.read(1.0)
            # Launching lays out the folder the presets live in.
            for folder in ["Presets", "Presets/MlaPlugins/MlaDrum", "Presets/MlaPlugins/MlaVerb",
                           "Presets/MlaPlugins/MlaDistortion", "Presets/MlaPlugins/MlaDelay"]:
                assert (root / folder).is_dir(), folder
            # Settings shows that folder and remembers what is applied there.
            frame = expect(tui.send(F1 + b"jjjjj\r", 0.9), b"Mlacker folder", os.fsencode(root.name))
            tui.send(b"\t\t\r", 0.6)              # buffer size dropdown
            tui.send(b"j\r", 0.6)                 # one size up
            expect(tui.send(b"\t\t\t\r", 1.0), b"Settings applied")
            saved = (root / ".mlacker.conf").read_text()
            assert saved.startswith("# mlacker settings"), saved
            assert 'root = "' + str(root) + '"' in saved, saved
            assert "buffer_frames = 256" in saved, saved
            tui.send(b"q", 0.5)
        finally:
            tui.close()

        # A configuration naming hardware this machine does not have falls back
        # to the defaults and asks before overwriting the file.
        (root / ".mlacker.conf").write_text(
            '# mlacker settings\n\n[paths]\nroot = "%s"\n\n[midi]\ninput = "Ghost MIDI Port"\n\n'
            '[audio]\noutput = "Ghost Interface"\nbuffer_frames = 512\nsample_rate = 44100\n' % root)
        tui = Terminal()
        try:
            tui.read(1.0)
            tui.send(F1 + b"jjjjj\r", 0.9)
            frame = expect(tui.send(TO_BUTTONS + b"\r", 1.0),
                           b"Some of the previously saved devices are not available")
            assert b"Save" in frame and b"Cancel" in frame, frame[-3000:]
            # Cancelling keeps the stored file exactly as it was.
            expect(tui.send(b"l\r", 0.8), b"the saved file was left alone")
            kept = (root / ".mlacker.conf").read_text()
            assert "Ghost Interface" in kept, kept
            # Saving replaces the missing devices with what is actually running.
            tui.send(F1 + b"jjjjj\r", 0.9)
            expect(tui.send(TO_BUTTONS + b"\r", 1.0), b"Some of the previously saved devices")
            tui.send(b"\r", 1.0)
            written = (root / ".mlacker.conf").read_text()
            assert "Ghost Interface" not in written, written
            assert "# mlacker settings" in written, written
            tui.send(b"q", 0.5)
        finally:
            tui.close()
    print("PASS: Mlacker folder layout, settings file, and missing-device prompt")


if __name__ == "__main__":
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    main()
