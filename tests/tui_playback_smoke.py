"""PTY regression for the BPM dialog, playback following, and sample following.

Usage: python3 tests/tui_playback_smoke.py /tmp/mlang_tui_demo
"""
import fcntl
import os
import re
import select
import struct
import subprocess
import sys
import tempfile
import termios
import time
import wave


def main():
    master, slave = os.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 100, 0, 0))
    env = dict(os.environ, TERM="xterm-256color", COLORTERM="truecolor")
    env.pop("NO_COLOR", None)
    process = subprocess.Popen([sys.argv[1]], stdin=slave, stdout=slave, stderr=slave, env=env)
    pending = bytearray()

    def frame():
        nonlocal pending
        deadline = time.monotonic() + 5
        while b"\x1b[0m" not in pending:
            assert time.monotonic() < deadline, pending[-500:]
            if select.select([master], [], [], 0.1)[0]:
                pending.extend(os.read(master, 65536))
        end = pending.index(b"\x1b[0m") + 4
        raw = bytes(pending[:end])
        del pending[:end]
        return re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", raw).decode()

    def until(predicate):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            text = frame()
            if predicate(text):
                return text
        raise AssertionError("Expected frame did not arrive")

    def send(keys):
        os.write(master, keys)

    try:
        until(lambda s: "New session" in s)
        send(b"\x1b")
        until(lambda s: "New session" not in s)
        send(b"\tll")
        until(lambda s: "Meter" in s)
        send(b"jjljj\r")
        until(lambda s: "Set update rate" in s and "FPS" in s)
        send(b"\x150\r")
        until(lambda s: "whole number from 1 to 240" in s)
        send(b"\x1560\r")
        until(lambda s: "Set update rate" not in s and "BPM 120" in s)
        send(b"\x1b[108;6um\x1b[98;5u")
        until(lambda s: "Set BPM" in s)
        send(b"\x150\r")
        until(lambda s: "whole number" in s)
        send(b"\x15400\r")
        until(lambda s: "BPM 400" in s and "Set BPM" not in s)
        send(b" ")
        until(lambda s: "PLAY" in s)
        moved = until(lambda s: (m := re.search(r"(\d{3}) C-4", s)) is not None and int(m[1]) > 2)
        assert "Time 00:00.000" not in moved
        assert any(0x2800 < ord(c) <= 0x28ff for c in moved), "MIDI meters stayed silent"
        send(b"\x1b[98;5u")
        until(lambda s: "Set BPM" in s and "PLAY" in s)
        send(b"\x15200\r")
        until(lambda s: "BPM 200" in s and "Set BPM" not in s and "PLAY" in s)
        send(b" ")
        until(lambda s: "STOP" in s)
        send(b"\t")
        until(lambda s: "New session" in s)
        send(b" ")
        assert "STOP" in until(lambda s: "New session" in s)  # menu owns Space
        send(b"\x1b")
        until(lambda s: "New session" not in s)
        send(b"\x02")  # legacy Ctrl+B
        until(lambda s: "Set BPM" in s)
        send(b"\x15137\x1b")
        until(lambda s: "Set BPM" not in s and "BPM 200" in s)

        # Add an audio instance and follow it at sub-row resolution while zoomed.
        send(b"gg\tlll")
        until(lambda s: "Create track" in s)
        send(b"jlj\r")
        until(lambda s: "Audio 4" in s and "Create track" not in s)
        with tempfile.TemporaryDirectory(prefix="mlang-playback-") as folder:
            path = os.path.join(folder, "follow.wav")
            with wave.open(path, "wb") as wav:
                wav.setparams((1, 2, 8000, 0, "NONE", "not compressed"))
                wav.writeframes(struct.pack("<h", 16384) * 72000)
            send(b"\tlllll\r")
            until(lambda s: "Add audio" in s)
            send(b"\x15" + path.encode() + b"\r")
            until(lambda s: "WAVE" in s and "Add audio" not in s)
            send(b"s" + b"\x1b[108;5u" * 4)
            before = until(lambda s: " Sample " in s)
            send(b" ")
            followed = until(lambda s: "PLAY" in s and " Sample " in s and
                             (m := re.search(r"\| (\d+)\.\.(\d+) ms", s)) is not None and int(m[1]) > 0)
            assert followed != before
            send(b" ")
            until(lambda s: "STOP" in s)
        def length_dialog():
            send(b"\tllll")
            until(lambda s: "Clone pattern" in s)
            send(b"jjjj\r")
            return until(lambda s: "Pattern length (1-16384 rows)" in s)

        length_dialog()
        send(b"\x150\r")
        until(lambda s: "whole number from 1 to 16384" in s)
        send(b"\x1532\r")
        until(lambda s: "Are you sure, data will be truncated" in s)
        send(b"\x1b")
        until(lambda s: "Set length" not in s)
        assert "120" in length_dialog()  # cancel preserved the imported pattern length
        send(b"\x1532\r")
        until(lambda s: "Are you sure, data will be truncated" in s)
        send(b"\r")
        until(lambda s: "Set length" not in s)
        assert "32" in length_dialog()
        send(b"\x1564\r")
        until(lambda s: "Set length" not in s)
        assert "64" in length_dialog()
        send(b"\x1532\r")  # newly appended empty rows need no confirmation
        until(lambda s: "Set length" not in s)
        send(b"q")
        # Keep draining the PTY while exiting: at high FPS a final frame can
        # otherwise fill its output buffer before the process handles Quit.
        deadline = time.monotonic() + 5
        while process.poll() is None and time.monotonic() < deadline:
            if select.select([master], [], [], 0.05)[0]:
                os.read(master, 65536)
        assert process.wait(timeout=5) == 0
        print("PASS: BPM validation/cancel, clock scrolling, MIDI meters, modal capture, sample following")
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
        os.close(master)
        os.close(slave)


if __name__ == "__main__":
    main()
