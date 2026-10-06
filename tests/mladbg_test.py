"""Integration checks against real MLang DWARF and the LLDB process engine."""
import argparse
import fcntl
import os
from pathlib import Path
import pty
import select
import struct
import subprocess
import tempfile
import termios
import time


def run(args, expected=0, env=None):
    result = subprocess.run(args, capture_output=True, text=True, timeout=60, env=env)
    output = result.stdout + result.stderr
    assert result.returncode == expected, "%r returned %s:\n%s" % (args, result.returncode, output)
    return output


def check_tui(debugger, executable, source):
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 100, 0, 0))
    env = dict(os.environ, TERM="xterm-256color")
    process = subprocess.Popen([debugger, "-ex", "b " + source + ":13", executable],
                               stdin=slave, stdout=slave, stderr=slave, env=env)
    os.close(slave)
    output = bytearray()

    def wait_for(text):
        deadline = time.monotonic() + 15
        while text not in output and time.monotonic() < deadline:
            if select.select([master], [], [], 0.1)[0]:
                try:
                    output.extend(os.read(master, 65536))
                except OSError:
                    break
        assert text in output, "TUI did not display %r:\n%s" % (text, output.decode(errors="replace"))

    try:
        wait_for(b"Console / program output")
        os.write(master, b":run\n")
        wait_for(b"count = 7")
        wait_for(b"ratio = 1.5")
        os.write(master, b":next\n")
        wait_for(b"step over")
        os.write(master, b":p count\n")
        wait_for(b"count = 12")
        os.write(master, b"q")
        deadline = time.monotonic() + 10
        while process.poll() is None and time.monotonic() < deadline:
            # Drain redraw output while quitting; a full PTY buffer can block
            # curses before it has a chance to consume the queued q key.
            if select.select([master], [], [], 0.1)[0]:
                try:
                    output.extend(os.read(master, 65536))
                except OSError:
                    break
        assert process.wait(timeout=2) == 0
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--debugger", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--source", required=True)
    options = parser.parse_args()
    debugger = str(Path(options.debugger).resolve())
    compiler = str(Path(options.compiler).resolve())
    source = str(Path(options.source).resolve())
    assert "MLang terminal debugger" in run([debugger, "--help"])
    assert "mladbg" in run([debugger, "--version"])
    with tempfile.TemporaryDirectory(prefix="mladbg-test-") as temporary:
        executable = str(Path(temporary) / "demo")
        run([compiler, "-g", "-O0", source, "-o", executable])

        def debug(commands, expected=0):
            args = [debugger, "--batch"]
            for command in commands:
                args.extend(["-ex", command])
            return run(args + [executable], expected)

        output = debug(["b " + source + ":13", "run", "locals", "p count", "bt",
                        "step", "next", "next", "locals", "finish", "next", "p count",
                        "registers", "disassemble", "continue"])
        for expected in ("Breakpoint 1: 1 location(s)", "count = 7", "unsigned_value = 42",
                         "enabled = true", "ratio = 1.5", "left = 7", "right = 5",
                         "total = 12", "count = 12", "main", "Process exited with status 0"):
            assert expected in output, "Missing %r:\n%s" % (expected, output)
        output = debug(["b add", "run", "next", "locals", "continue"])
        assert "Breakpoint 1: 1 location(s)" in output, output
        assert "left = 7" in output and "right = 5" in output, output
        output = debug(["b " + source + ":13", "disable 1", "run"])
        assert "Stopped: breakpoint" not in output, output
        assert "Process exited with status 0" in output, output
        output = debug(["b " + source + ":13", "run", "watch count", "continue",
                        "p count", "continue"])
        assert "watchpoint" in output.lower() and "count = 12" in output, output
        debug(["not-a-real-command"], expected=1)
        run([debugger, executable], expected=2)  # Non-TTY invocation must fail clearly.
        check_tui(debugger, executable, source)
    print("mladbg integration checks passed")


if __name__ == "__main__":
    main()
