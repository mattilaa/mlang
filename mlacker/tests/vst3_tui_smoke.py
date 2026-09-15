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
        assert b"Load master VST3" in send(b"lllllj\r")
        frame = send(b"\x15" + os.fsencode(os.path.abspath(sys.argv[2])) + b"\r", 1)
        assert b"Master VST3: Mlacker Test Instrument" in frame, frame[-2000:]
        assert b"audio output disabled" in frame
        with tempfile.TemporaryDirectory(prefix="mlacker-vst3-") as directory:
            bad_bundle = os.path.join(directory, "Broken.vst3")
            os.mkdir(bad_bundle)
            assert b"Load master VST3" in send(b"\tlllllj\r")
            frame = send(b"\x15" + os.fsencode(bad_bundle) + b"\r", 1)
            assert b"VST3 load failed" in frame
        assert b"Master VST3 unloaded" in send(b"\tllllljj\r", 0.5)
        send(b"q")
        deadline = time.monotonic() + 3
        while process.poll() is None and time.monotonic() < deadline:
            read_for(0.1)
        assert process.poll() == 0
        print("PASS: mlacker VST3 bundle chooser, load, failed replacement, unload, shutdown")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)
        os.close(slave)


if __name__ == "__main__":
    main()
