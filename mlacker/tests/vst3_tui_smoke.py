"""Load an actual bundle through mlacker's TUI, without opening audio devices."""
import fcntl
import os
import select
import struct
import subprocess
import sys
import tempfile
import termios
import time


def main():
    master, slave = os.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 28, 110, 0, 0))
    env = dict(os.environ, TERM="xterm-256color", MLANG_TUI_NO_HARDWARE="1")
    env.pop("NO_COLOR", None)
    process = subprocess.Popen([sys.argv[1]], stdin=slave, stdout=slave, stderr=slave, env=env)

    def read_for(seconds=0.3):
        data = bytearray()
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            ready, _, _ = select.select([master], [], [], min(0.05, max(0, deadline - time.monotonic())))
            if ready:
                data.extend(os.read(master, 65536))
        return bytes(data)

    def send(keys, seconds=0.3):
        os.write(master, keys)
        return read_for(seconds)

    try:
        assert b"mlacker" in read_for(1)
        assert b"Load master VST3" in send(b"llllllj\r")
        frame = send(b"\x15" + os.fsencode(os.path.abspath(sys.argv[2])) + b"\r", 1)
        assert b"Master VST3: Mlacker Test Instrument" in frame, frame[-2000:]
        assert b"audio output disabled" in frame
        with tempfile.TemporaryDirectory(prefix="mlacker-vst3-") as directory:
            bad_bundle = os.path.join(directory, "Broken.vst3")
            os.mkdir(bad_bundle)
            assert b"Load master VST3" in send(b"\tllllllj\r")
            frame = send(b"\x15" + os.fsencode(bad_bundle) + b"\r", 1)
            assert b"VST3 load failed" in frame
        assert b"Master VST3 unloaded" in send(b"\tlllllljj\r", 0.5)
        frame = send(b"\tllljljj\r", 0.5)
        assert b"Instrument track created" in frame, frame[-2000:]
        assert b"Instruments" in frame
        assert b"Add VST3 instrument" in send(b"\tlllllljjj\r")
        frame = send(b"\x15" + os.fsencode(os.path.abspath(sys.argv[2])) + b"\r", 1)
        assert b"Instrument loaded: Mlacker Test Instrument" in frame, frame[-2000:]
        assert b"001 Mlacker Test" in frame, frame[-6000:]
        assert b"Instrument: 1" in frame, frame[-6000:]
        # Switch away and return via View > Instruments (last View entry).
        send(b"\tlljjj\r")  # Show patterns
        frame = send(b"\tlljjjjjjj\r")
        assert b"Instruments" in frame and b"001 Mlacker Test" in frame, frame[-6000:]
        # The list owns normal navigation and Enter, without changing pattern.
        send(b"\x1b[104;6u")  # Ctrl+Shift+H: focus left
        frame = send(b"ggG\r")
        assert b"Instrument: 1" in frame
        frame = send(b"m")
        assert b"Mixer" in frame and b"Master" in frame and b"L R" in frame
        frame = send(b"\tlllllll\r")
        assert b"VST3 editor:" in frame and b"Modulation" in frame, frame[-6000:]
        frame = send(b"J\r\x151.5\r")
        assert b"Invalid value" in frame, frame[-6000:]
        frame = send(b"\x150.25\r")
        assert b"0.25" in frame, frame[-6000:]
        send(b"\x1b", 0.4)
        frame = send(b"\tlllllll\r")
        assert b"VST3 editor:" in frame and b"0.25" in frame, frame[-6000:]
        frame = send(b"L")
        assert b"255;255;255" in frame and b"192;32;48" in frame, frame[-6000:]
        frame = send(b"\x1b[108;2u")  # Shift+L via CSI-u also cancels.
        assert b"192;32;48" not in frame
        send(b"L")
        frame = send(b"l")  # Lowercase l navigates and cancels learning.
        assert b"192;32;48" not in frame
        frame = send(b"\x1b[C\x1b[C\x1b[C")
        assert b"Extra control" in frame and b"VST3 editor:" in frame
        send(b"\x1b", 0.4)
        send(b"m")  # Inspector displays command status.
        frame = send(b"\tlllllllj\r")
        assert b"Instrument removed; track assignments cleared" in frame, frame[-6000:]
        # Slot reuse must not shift IDs or retain the old assignment.
        assert b"Add VST3 instrument" in send(b"\tlllllljjj\r")
        frame = send(b"\x15" + os.fsencode(os.path.abspath(sys.argv[2])) + b"\r", 1)
        assert b"001 Mlacker Test" in frame and b"Instrument: 1" in frame, frame[-6000:]
        send(b"q")
        deadline = time.monotonic() + 3
        while process.poll() is None and time.monotonic() < deadline:
            read_for(0.1)
        assert process.poll() == 0
        print("PASS: master and instrument bundle loading, instrument track assignment, Instruments view, shutdown")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)
        os.close(slave)


if __name__ == "__main__":
    main()
