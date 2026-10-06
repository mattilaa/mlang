"""Integration checks against real MLang DWARF and the LLDB process engine."""
import argparse
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import signal
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
        wait_for(b"Ready")
        assert b"\x1b(0" in output or "┌".encode() in output, "Pane borders were not drawn"
        os.write(master, b"\x1bOP")  # F1 in xterm-256color.
        wait_for(b"MLADBG HELP")
        os.write(master, b"?")  # Closing help must leave the session usable.
        os.write(master, b":run\n")
        wait_for(b"count = 7")
        wait_for(b"ratio = 1.5")
        # Exercise the smallest supported layout with a stopped process.
        fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 18, 70, 0, 0))
        os.kill(process.pid, signal.SIGWINCH)
        os.write(master, b"ljjkh:next\n")
        wait_for(b"step over")
        # h/j/k/l must remain literal text inside the command prompt.
        os.write(master, b":help frames\n")
        wait_for(b"Frame 0 is the current function")
        os.write(master, b"8")
        wait_for(b"KEYBOARD AND COMMAND ENTRY")
        os.write(master, b"\x1b:p count\n")
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

        def debug(commands, expected=0, program=None):
            args = [debugger, "--batch"]
            for command in commands:
                args.extend(["-ex", command])
            return run(args + [program or executable], expected)

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
        output = debug(["help frames", "help execution", "help variables", "help breakpoints",
                        "help threads", "help memory", "help session", "help keys",
                        "help lldb frame select"])
        for token in ("Frame 0 is the current function", "up", "down", "p team.members",
                      "breakpoint modify", "watchpoint delete", "command history",
                      "KEYBOARD AND COMMAND ENTRY", "frame select"):
            assert token.lower() in output.lower(), "Missing help %r:\n%s" % (token, output)
        debug(["help nonexistent-topic"], expected=1)
        output = debug(["b " + source + ":13", "run", "watch count", "continue",
                        "p count", "continue"])
        assert "watchpoint" in output.lower() and "count = 12" in output, output
        debug(["not-a-real-command"], expected=1)
        run([debugger, executable], expected=2)  # Non-TTY invocation must fail clearly.
        check_tui(debugger, executable, source)
        complex_source = Path(__file__).parent / "fixtures" / "mladbg_complex.mla"
        complex_executable = str(Path(temporary) / "complex")
        run([compiler, "-g", "-O0", str(complex_source), "-o", complex_executable])

        def marker(name):
            return next(i for i, line in enumerate(complex_source.read_text().splitlines(), 1)
                        if name in line)

        output = debug(["b " + str(complex_source) + ":" + str(marker("complex-break")),
                        "b " + str(complex_source) + ":" + str(marker("parameter-break")),
                        "run", "locals", "p point.x", "p numbers.data[1]",
                        "p mapping.values[0].x", "p pair._0", "p *pointer",
                        "p empty.len = -1", "p empty", "p empty.len = 1",
                        "p empty.data = 0", "p empty", "p empty.len = 0",
                        "continue", "locals", "continue"], program=complex_executable)

        def block(name):
            match = re.search(r"^\([^\n]+\) " + re.escape(name) + r" = .*\n(?:  .*\n)*", output, re.M)
            assert match, "Missing variable %s:\n%s" % (name, output)
            return match.group()

        for name, tokens in {
            "point": ["x = -3", "y = 42"],
            "shape": ["position = {", "visible = true", "selected = false", "weight = 1.5"],
            "boxed": ["value = {", "x = -3"],
            "tagged": ["x = -8", "y = 80", "tag = 3"],
            "numbers": ["len=3", "[1] = 9"],
            "empty": ["len=0"],
            "fixed": ["len=3", "[2] = 30"],
            "points": ["len=2", "x = 5", "y = 99"],
            "mapping": ['[0].key = "first"', "[0].value = {", "x = -3"],
            "empty_mapping": ["len=0"],
            "nested": ["len=1", "len=2", "[1] = 2"],
            "pair": ["_0 = 12", '_1 = "tuple text"'],
            "label": ['"hello debugger"'],
            "wide": ['"wide text"'],
            "color": ["Blue"],
            "inferred_numbers": ["len=2", "[1] = 14"],
            "inferred_mapping": ['[0].key = "second"', "x = 8", "y = 88"],
            "inferred_pair": ["_0 = 13", '"inferred tuple"'],
            "many": ["len=18", "[15] = 15", "... 2 more element(s)"],
        }.items():
            for token in tokens:
                assert token in block(name), "Missing %r in %s:\n%s" % (token, name, output)
        assert "<invalid length -1>" in output, output
        assert "<unavailable data>" in output, output
        assert output.count("shape = {") >= 2, output  # Also inspect by-value parameters.
        assert "Process exited with status 0" in output, output
        demo_source = Path(source).parent / "debugger_demo.mla"
        demo_executable = str(Path(temporary) / "debugger-demo")
        run([compiler, "-g", "-O0", str(demo_source), "-o", demo_executable])
        demo_line = next(i for i, line in enumerate(demo_source.read_text().splitlines(), 1)
                         if "demo-break" in line)
        output = debug(["b " + str(demo_source) + ":" + str(demo_line), "run",
                        "locals", "p team.members", "p team.ratings", "p tasks",
                        "p checkpoint", "p summary", "p stage",
                        "p lead_pointer->position.x", "p team.members.data[1].name",
                        "bt", "continue"], program=demo_executable)
        for token in ('name = "Ada"', 'name = "Linus"', "checkpoint = {", "stage = Review",
                      "members = len=2", "ratings = len=2", '[1].key = "Linus"',
                      "[1].value = 88", "effort = 16", "Total effort: 16"):
            assert token in output, "Missing demo value %r:\n%s" % (token, output)
        stack_line = next(i for i, line in enumerate(demo_source.read_text().splitlines(), 1)
                          if "stack-break" in line)
        output = debug(["b " + str(demo_source) + ":" + str(stack_line), "run", "bt",
                        "frame 0", "locals", "p engineer", "p hours", "p adjusted", "p snapshot",
                        "frame 1", "locals", "p details", "p requested", "p budget", "p remaining",
                        "frame 2", "locals", "p team.members", "p cycle", "p policy", "p total",
                        "frame 3", "locals", "p team.ratings", "p tasks", "p effort", "p stage",
                        "frame 4", "locals", "p batch_name", "p started_at",
                        "frame 2", "up", "p effort", "down", "p cycle",
                        "frame 0", "finish", "next", "p result", "continue"],
                       program=demo_executable)
        for index, function in enumerate(("finalize_work", "score_work", "plan_work", "review", "main")):
            assert re.search(r"frame #%d:.*`%s" % (index, function), output), output
        for token in ("adjusted = 20", "remaining = 8", "budget = 24", "hours = 16",
                      "effort = 16", 'batch_name = "Weekly rollout"', "started_at = 9",
                      "snapshot = {", "details = {", "cycle = {", "policy = {", "result = 16"):
            assert token in output, "Missing stack value %r:\n%s" % (token, output)
        assert "Process exited with status 0" in output, output
    print("mladbg integration checks passed")


if __name__ == "__main__":
    main()
