"""Pattern menu save/load and invalid-file handling without audio hardware."""
import os
import tempfile
from pathlib import Path
from session_tui_smoke import Terminal, F1


def main():
    with tempfile.TemporaryDirectory(prefix="mlacker-pattern-") as folder:
        tui = Terminal()
        save = F1 + b"llll" + b"j" * 7 + b"\r"
        load = F1 + b"llll" + b"j" * 8 + b"\r"
        try:
            tui.read(.8)
            assert b"Instrument track created" in tui.send(F1 + b"lll" + b"jj\r")
            base = Path(folder) / "pattern"
            assert b"Save pattern (.mlapatt)" in tui.send(save)
            frame = tui.send(b"\x15" + os.fsencode(base) + b"\r", .6)
            assert b"Saved pattern" in frame, frame[-4000:]
            path = base.with_suffix(".mlapatt")
            assert b"MLAPATT" in path.read_bytes()
            assert b"Load pattern (.mlapatt)" in tui.send(load)
            frame = tui.send(b"\x15" + os.fsencode(path) + b"\r", .6)
            assert b"Loaded pattern" in frame and b"002" in frame, frame[-4000:]
            upper = Path(folder) / "upper.MLAPATT"
            tui.send(save)
            tui.send(b"\x15" + os.fsencode(upper) + b"\r", .6)
            assert upper.exists() and not Path(str(upper) + ".mlapatt").exists()
            invalid = Path(folder) / "invalid.mlapatt"
            invalid.write_bytes(path.read_bytes()[:-1])
            tui.send(load)
            frame = tui.send(b"\x15" + os.fsencode(invalid) + b"\r", .6)
            assert b"Invalid, truncated or unsupported" in frame, frame[-4000:]
            assert b"003 Untitled" not in frame, frame[-4000:]
        finally:
            tui.close()


if __name__ == "__main__":
    main()
