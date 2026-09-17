"""Exercise the built TUI demo in a real PTY, without third-party packages.

Usage: python3 tests/tui_terminal_smoke.py /tmp/mlang_tui_demo
"""
import fcntl
import os
import re
import select
import struct
import subprocess
import sys
import termios
import time
import tempfile
import wave


def main():
    master, slave = os.openpty()
    before = termios.tcgetattr(slave)
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
    env = dict(os.environ, TERM="xterm-256color", COLORTERM="truecolor")
    env["MLANG_TUI_NO_HARDWARE"] = "1"
    env.pop("NO_COLOR", None)
    process = subprocess.Popen([sys.argv[1]], stdin=slave, stdout=slave, stderr=slave, env=env)
    pending_output = bytearray()

    def read_until(needle, timeout=5):
        nonlocal pending_output
        data = pending_output
        pending_output = bytearray()
        deadline = time.monotonic() + timeout
        while needle not in data and time.monotonic() < deadline:
            ready, _, _ = select.select([master], [], [], 0.1)
            if ready:
                data.extend(os.read(master, 65536))
            elif process.poll() is not None:
                break
        assert needle in data, f"Missing {needle!r}; output tail: {data[-300:]!r}"
        end = data.index(needle) + len(needle)
        pending_output = data[end:]
        return data[:end]

    def read_frame(expected_pane, dialog_pane=None, table_cell=None, table_text=None, pattern_grainy=False):
        frame = read_until(b"\x1b[0m")
        # Inspect rendered border colors, not application debug/status text.
        x = y = 0
        foreground = None
        background = None
        backgrounds = {}
        glyphs = {}
        cells = {}
        for token in re.split(r"(\x1b\[[0-?]*[ -/]*[@-~])", frame.decode()):
            if token.startswith("\x1b["):
                params = token[2:-1]
                if token.endswith("H"):
                    row, col = map(int, params.split(";"))
                    x, y = col - 1, row - 1
                elif token.endswith("m") and params.startswith("38;2;"):
                    foreground = tuple(map(int, params.split(";")[2:]))
                elif token.endswith("m") and params.startswith("48;2;"):
                    background = tuple(map(int, params.split(";")[2:]))
            else:
                for _glyph in token:
                    cells[x, y] = foreground
                    backgrounds[x, y] = background
                    glyphs[x, y] = _glyph
                    x += 1
        borders = [(0, 8), (23, 8), (23, 18)]
        selected = [i for i, point in enumerate(borders) if cells.get(point) == (145, 184, 235)]
        assert selected == ([] if expected_pane is None else [expected_pane]), selected
        if table_cell is not None:
            point, expected = table_cell
            assert backgrounds.get(point) == expected, (point, backgrounds.get(point), expected)
        if table_text is not None:
            (x, y), expected = table_text
            actual = "".join(glyphs.get((x + i, y), "") for i in range(len(expected)))
            assert actual == expected, (actual, expected)
        if pattern_grainy:
            assert any(0x2800 < ord(glyph) <= 0x28ff for (x, y), glyph in glyphs.items()
                       if x >= 28 and 4 <= y <= 13), "Missing sub-row Braille waveform"
        if dialog_pane is not None:
            dialog_borders = [(3, 3), (3, 8), (28, 8)]
            dialog_selected = [i for i, point in enumerate(dialog_borders)
                               if cells.get(point) == (145, 184, 235)]
            assert dialog_selected == [dialog_pane], dialog_selected
        return frame

    try:
        frame = read_frame(None)
        assert b"\x1b[?1049h" in frame and b"\x1b[?25l" in frame
        assert b"38;2;" in frame and b"48;2;" in frame
        assert "┌".encode() in frame and b"New session" in frame
        assert b"\x1b[>1u" in frame
        assert b" BPM 120 | Time 00:00.000 | 4/4 | MIDI IN " in frame
        assert "○" in frame.decode()
        assert b"w/b: col" not in frame
        # Switch to Edit, activate Undo, then reopen and dismiss with Escape.
        os.write(master, b"\x1b[C")
        assert b"Undo" in read_frame(None)
        os.write(master, b"\r")
        assert b"Command selected" in read_frame(0)
        # Pattern selection updates the editor immediately; list shortcuts work.
        os.write(master, b"j")
        assert b"Pattern / 2 Verse" in read_frame(0, table_cell=((1, 3), (44, 77, 118)), table_text=((28, 4), "E-4"))
        os.write(master, b"G")
        assert b"Pattern / 3 Chorus" in read_frame(0, table_text=((28, 4), "G#4"))
        os.write(master, b"gg")
        assert b"Pattern / 1 Intro" in read_frame(0, table_text=((28, 4), "C-4"))

        def pattern_menu(item):
            os.write(master, b"\tllll")
            assert b"Add pattern" in read_frame(None)
            os.write(master, b"j" * item + b"\r")

        pattern_menu(3)
        assert b"Pattern / 4 Intro copy" in read_frame(0)
        pattern_menu(1)
        assert b"Rename pattern" in read_frame(None)
        os.write(master, b"\x15Break\r")
        assert b"004 Break" in read_frame(0)
        pattern_menu(2)
        assert b"004 Break" not in read_frame(0)
        pattern_menu(0)
        assert b"Pattern / 5 Pattern 5" in read_frame(0, table_text=((28, 4), "   "))
        os.write(master, b"dd")
        assert b"005 Pattern 5" not in read_frame(0)
        os.write(master, b"gg")
        read_frame(0)
        os.write(master, b"\tll")
        assert b"Show song" in read_frame(None)
        os.write(master, b"jjjj\r")
        song = read_frame(0)
        assert b" Song " in song and b"2  002 Verse" in song and b"3  002 Verse" in song
        os.write(master, b"\x1b[106;5u")
        assert b"2  001 Intro" in read_frame(0)
        os.write(master, b"\x1b[107;5u")
        assert b"1  001 Intro" in read_frame(0)
        os.write(master, b"o")
        assert b"2  001 Intro" in read_frame(0)
        os.write(master, b"O")
        assert b"3  001 Intro" in read_frame(0)
        os.write(master, b"K")
        changed = read_frame(0)
        assert b"2  002 Verse" in changed and b"Pattern / 2 Verse" in changed
        os.write(master, b"J")
        changed = read_frame(0)
        assert b"2  001 Intro" in changed and b"Pattern / 1 Intro" in changed
        os.write(master, b"ddddgg")
        assert b"2  002 Verse" in read_frame(0)
        os.write(master, b"jj")
        assert b"Pattern / 2 Verse" in read_frame(0)
        os.write(master, b"dd")
        assert b"3  003 Chorus" in read_frame(0)
        os.write(master, b"\tll")
        read_frame(None)
        os.write(master, b"jjj\r")
        assert b" Patterns " in read_frame(0)
        os.write(master, b"gg")
        read_frame(0)
        # The sequence is a real 64-row table; column/row input stays in its pane.
        os.write(master, b"\x1b[108;6u")
        read_frame(1)
        os.write(master, b"G")
        assert b"064" in read_frame(1)
        os.write(master, b"gg")
        read_frame(1, table_text=((24, 4), "001"))
        os.write(master, b"m")
        mixer_frame = read_frame(1)
        assert b" Mixer " in mixer_frame and b"VOL" in mixer_frame and b"100" in mixer_frame and b"P0" in mixer_frame
        assert b"STOP" in mixer_frame  # no synthetic animation while stopped
        os.write(master, b"\tll")
        assert b"Meter" in read_frame(None)
        os.write(master, b"jjl")
        assert b"Grainy (osc)" in read_frame(None)
        os.write(master, b"j\r")
        grainy = read_frame(1).decode()
        assert any("\u2800" <= glyph <= "\u28ff" for glyph in grainy)
        os.write(master, b"\tll")
        read_frame(None)
        os.write(master, b"jjl")
        read_frame(None)
        os.write(master, b"\r")
        solid = read_frame(1).decode()
        # Note-duration rails use Braille independently of the meter style.
        assert not any("\u2800" <= glyph <= "\u28ff" for glyph in solid.split(" Mixer ", 1)[1])
        os.write(master, b"m")
        while b" Inspector " not in read_frame(1):
            pass
        # MIDI starts compact; z cycles only this track, Z cycles all tracks.
        os.write(master, b"z")
        read_frame(1, table_text=((39, 3), "LEN"))
        os.write(master, b"z")
        read_frame(1, table_text=((51, 3), "CC1"))
        os.write(master, b"z")
        read_frame(1, table_text=((39, 3), "NOTE"))
        os.write(master, b"Z")
        read_frame(1, table_text=((39, 3), "LEN"))
        os.write(master, b"Z")
        read_frame(1, table_text=((51, 3), "CC1"))
        os.write(master, b"l")
        read_frame(1, table_cell=((34, 4), (60, 91, 128)))
        os.write(master, b"j")
        read_frame(1, table_cell=((34, 5), (60, 91, 128)))
        os.write(master, b"j" * 70)
        assert b"064" in read_frame(1)
        os.write(master, b"h" + b"k" * 70)
        assert b"001" in read_frame(1, table_cell=((28, 4), (60, 91, 128)))
        os.write(master, b"K")
        read_frame(1, table_text=((28, 4), "C#4"))
        os.write(master, b"\x1b[106;2u")
        read_frame(1, table_text=((28, 4), "C-4"))
        os.write(master, b"l\r")
        assert b"Editing:" in read_frame(1)
        os.write(master, b"\x15128\r")
        assert b"Invalid type" in read_frame(1)
        os.write(master, b"\x1542\r")
        read_frame(1, table_text=((34, 4), "42"))
        os.write(master, b"\r\x1599")
        read_frame(1)
        os.write(master, b"\x1b")
        read_frame(1, table_text=((34, 4), "42"))
        # CC cells reject text and remain editable until corrected.
        os.write(master, b"lll\r\x15qhjk\r")
        assert b"Invalid type" in read_frame(1)
        os.write(master, b"\x1564\r")
        read_frame(1, table_text=((51, 4), "64"))
        os.write(master, b"hhhh")
        read_frame(1, table_cell=((28, 4), (60, 91, 128)))
        # Empty note commits as a rest; clearing preserves ROW, dd removes it.
        os.write(master, b"\r\x15\r")
        read_frame(1, table_text=((28, 4), "   "))
        os.write(master, b"\x1b[127;2u")
        read_frame(1, table_text=((34, 4), "   "))
        os.write(master, b"d")
        read_frame(1, table_text=((28, 4), "   "))
        os.write(master, b"d")
        read_frame(1, table_text=((28, 4), "C-4"))
        os.write(master, b"j" * 70)
        frame = read_frame(1)
        assert b"063" in frame and b"064" not in frame
        os.write(master, b"k" * 70)
        read_frame(1)
        # dd in an inline editor is text, not a row command.
        os.write(master, b"llll\r\x15dd\r")
        assert b"Invalid type" in read_frame(1)
        os.write(master, b"\x1b")
        read_frame(1)
        os.write(master, b"hhhh")
        read_frame(1)
        def track_menu(item):
            os.write(master, b"\tlll")
            assert b"Create track" in read_frame(None)
            os.write(master, b"j" * item + b"\r")

        # Track menu acts on the group containing the selected child column.
        track_menu(11)
        read_frame(1, table_text=((39, 3), "NOTE"))
        track_menu(12)
        read_frame(1, table_text=((51, 3), "CC1"))
        track_menu(8)
        read_frame(1, table_text=((51, 4), "   "))
        os.write(master, b"hhh")  # select VEL in the original note line
        read_frame(1)
        track_menu(10)
        read_frame(1, table_text=((51, 4), "C-4"))
        track_menu(9)
        read_frame(1, table_text=((51, 4), "   "))
        track_menu(9)
        read_frame(1, table_text=((28, 4), "C-4"))
        track_menu(0)
        assert b"Rename track" in read_frame(None)
        os.write(master, b"\x15Lead qhjk\r")
        assert b"Lead qhjk" in read_frame(1)
        track_menu(1)
        assert b"MIDI track" in read_frame(None)
        os.write(master, b"\r")
        created = read_frame(1, table_text=((24, 4), "001"))
        assert b"Track 4" in created
        os.write(master, b"zz")  # expand the newly created compact track
        read_frame(1)
        track_menu(3)
        assert b"Track 4 copy" in read_frame(1, table_text=((24, 4), "001"))
        track_menu(2)
        assert b"Delete track" in read_frame(None)
        os.write(master, b"\r")
        read_frame(None)
        while True:
            frame = read_until(b"\x1b[0m")
            if b"Delete track" not in frame:
                assert b"Track 4 copy" not in frame
                break
        track_menu(4)
        assert b"Clear track" in read_frame(None)
        os.write(master, b"\r")
        read_frame(None)
        while b"Clear track" in read_until(b"\x1b[0m"):
            pass
        track_menu(5)
        assert b"[M]" in read_frame(1)
        track_menu(6)
        assert b"Configure CC1" in read_frame(None)
        os.write(master, b"\x15pitchbend\r")
        assert b"CC1=pitchbend" in read_frame(1)
        track_menu(7)
        assert b"Configure CC2" in read_frame(None)
        os.write(master, b"\x15cutoff:0:1000\r")
        assert b"CC2=cutoff:0:1000" in read_frame(1)
        track_menu(1)
        assert b"AUDIO track" in read_frame(None)
        os.write(master, b"j\r")
        assert b"[AUDIO]" in read_frame(1)
        # Import stereo WAV and mono AIFF through Add -> Audio. No devices or
        # third-party codecs are needed; known amplitudes verify channel colors.
        with tempfile.TemporaryDirectory(prefix="mlang-tui-audio-") as fixture_dir:
            wav_path = os.path.join(fixture_dir, "stereo.WAV")
            with wave.open(wav_path, "wb") as wav:
                wav.setparams((2, 2, 8000, 0, "NONE", "not compressed"))
                wav.writeframes(struct.pack("<hh", 16384, -8192) * 72000)
            aif_path = os.path.join(fixture_dir, "mono.AIF")
            comm = struct.pack(">hIh", 1, 8000, 16) + bytes.fromhex("400bfa00000000000000")
            samples = struct.pack(">h", 16384) * 8000
            chunks = b"COMM" + struct.pack(">I", len(comm)) + comm
            chunks += b"SSND" + struct.pack(">I", len(samples) + 8) + bytes(8) + samples
            with open(aif_path, "wb") as fixture:
                fixture.write(b"FORM" + struct.pack(">I", len(chunks) + 4) + b"AIFF" + chunks)
            for path in (wav_path, aif_path):
                if path == aif_path:
                    os.write(master, b"\x7f")  # remove the WAV instance, not the loaded sample
                    assert b"L  WAVE" not in read_frame(1)
                    os.write(master, b"jjjjj")  # next import starts on row 6
                    read_frame(1)
                os.write(master, b"\tllllll\r")
                assert b"Add audio" in read_frame(None, 0)
                os.write(master, b"\x15" + path.encode() + b"\r")
                imported = read_frame(1)
                assert b"L  WAVE  R" in imported
                assert b"38;2;90;220;140" in imported  # left, green
                assert b"38;2;239;101;117" in imported  # right, red
                if path == wav_path:
                    assert any(0x2800 < ord(c) <= 0x28ff for c in imported.decode())
                os.write(master, b"z")
                assert b"L  WAVE x2  R" in read_frame(1)
                os.write(master, b"\t")
                read_frame(None)
                os.write(master, b"z")
                read_frame(None)
                os.write(master, b"\x1b")
                assert b"L  WAVE x2  R" in read_frame(1)  # menu captured z
                os.write(master, b"z")
                assert b"L  WAVE  R" in read_frame(1)
                os.write(master, b"s")
                assert b" Sample: " in read_frame(1)
                os.write(master, b"\x1b[108;5u" * 15)
                assert b"1 frames/px" in read_frame(1)
                os.write(master, b"\x1b[104;5u")
                assert b" Sample: " in read_frame(1)
                # Both views share the texture and type controls.
                os.write(master, b"\tll")
                read_frame(None)
                os.write(master, b"jjjjjlj\r")
                assert b"Grainy Filled" in read_frame(1, pattern_grainy=True)
                os.write(master, b"\tll")
                read_frame(None)
                os.write(master, b"jjjjjljjlj\r")
                assert b"Grainy Wave" in read_frame(1, pattern_grainy=True)
                # The lower pane can own zoom input too; pane navigation stays distinct.
                os.write(master, b"\x1b[106;6u")
                read_frame(2)
                os.write(master, b"\x1b[104;5u")
                assert b" Sample: " in read_frame(2)
                os.write(master, b"s")
                assert b" Inspector " in read_frame(2)
                os.write(master, b"\x1b[107;6u")
                read_frame(1)
                os.write(master, b"\tll")
                read_frame(None)
                os.write(master, b"jjjjjl\r")
                read_frame(1)
                os.write(master, b"\tll")
                read_frame(None)
                os.write(master, b"jjjjjljjl\r")
                read_frame(1)
                if path == aif_path:
                    os.write(master, b"s")
                    assert b" Sample: " in read_frame(1)
                    os.write(master, b"G")
                    assert b" Inspector " in read_frame(1)  # cursor past short clip
                    os.write(master, b"gg")
                    assert b" Sample: " not in read_frame(1)  # requires s to reopen
            os.write(master, b"G")
            assert b"072" in read_frame(1)
            os.write(master, b"gg")
            read_frame(1)
        # Loaded samples survive removal of their source files and placements.
        os.write(master, b"jjjjj\x7f")
        assert b"L  WAVE" not in read_frame(1)
        os.write(master, b"\tll")
        read_frame(None)
        os.write(master, b"jjjjjj\r")
        assert b" Audio " in read_frame(1)
        os.write(master, b"\x1b[104;6u")
        assert b"2 mono.AIF" in read_frame(0)
        os.write(master, b"\r")  # reuse selected sample at the Pattern cursor (row 6)
        assert b"L  WAVE" in read_frame(0)
        os.write(master, b"\x1b[108;6u" + b"j" * 15)
        read_frame(1)
        os.write(master, b"\x1b[104;6u\r")  # a second independent instance at row 21
        assert b"L  WAVE" in read_frame(0)
        os.write(master, b"\x1b[108;6u\x7f")
        assert b"L  WAVE" in read_frame(1)  # the first instance remains
        os.write(master, b"gg\tll")
        read_frame(None)
        os.write(master, b"jjj\r")
        assert b" Patterns " in read_frame(1)
        os.write(master, b"m")
        assert b"A5" in read_frame(1)
        os.write(master, b"m")
        while b" Inspector " not in read_frame(1):
            pass
        os.write(master, b"h" * 30)
        read_frame(1, table_cell=((28, 4), (60, 91, 128)), table_text=((24, 4), "001"))
        # Menu navigation must not mutate the underlying table, even after
        # scrolling beyond its viewport; column and pane keys are captured too.
        os.write(master, b"\t")
        read_frame(None)
        os.write(master, b"j" * 70 + b"llGm\x1b[106;6u")
        read_frame(None)
        os.write(master, b"\x1b")
        assert b"001" in read_frame(1, table_cell=((28, 4), (60, 91, 128)))
        os.write(master, b"\x1b[104;6u")
        read_frame(0)
        # List navigation selects another pattern; its mixer is independent.
        os.write(master, b"jl")
        assert b"Pattern / 2 Verse" in read_frame(0)
        os.write(master, b"m")
        assert b"A5" not in read_frame(0)
        os.write(master, b"m")
        while b" Inspector " not in read_frame(0):
            pass
        os.write(master, b"\x1b[108;6u")
        read_frame(1, table_cell=((28, 4), (60, 91, 128)))
        os.write(master, b"\x1b[104;6u")
        read_frame(0)
        # Ctrl+Shift+L/J/K/H traverses the nested pane geometry.
        for packet, pane in [(b"\x1b[108;6u", 1), (b"\x1b[106;6u", 2),
                             (b"\x1b[107;6u", 1), (b"\x1b[104;6u", 0),
                             (b"\x1b[108;6u", 1), (b"\x1b[106;6u", 2)]:
            os.write(master, packet)
            read_frame(pane)
        os.write(master, b"\t")
        assert b"New session" in read_frame(None)
        os.write(master, b"\x1b[107;6u")
        read_frame(None)  # menu retains keyboard ownership
        os.write(master, b"\x1b")
        read_frame(2)  # previous pane restored, not the first pane
        for dismissal in (b"\t",):
            os.write(master, b"\t")
            read_frame(None)
            os.write(master, dismissal)
            read_frame(2)
        # Non-file modal: configurable base buttons keep focus out of the view.
        for answer, status in [(b"\r", b"confirmed"), (b"l\r", b"cancelled")]:
            os.write(master, b"\t")
            read_frame(None)
            os.write(master, b"\r")
            question = read_frame(None)
            assert b"Start a new session?" in question and b"Cancel" in question
            os.write(master, answer)
            pressed = read_frame(None)
            assert b"Start a new session?" in pressed
            # The modal remains visible during the press animation.
            while True:
                frame = read_until(b"\x1b[0m")
                if b"Start a new session?" not in frame:
                    assert status in frame
                    break
        # Cascades remain beside their ancestors. h closes only one level;
        # Escape from the grandchild closes every menu and restores pane 2.
        os.write(master, b"\t")
        read_frame(None)
        os.write(master, b"jjl")
        cascade = read_frame(None)
        assert b"Recent sessions" in cascade and b"Blue hour" in cascade and b">" in cascade
        os.write(master, b"jl")
        assert b"Ambient" in read_frame(None)
        os.write(master, b"h")
        parent = read_frame(None)
        assert b"Blue hour" in parent and b"Ambient" not in parent
        os.write(master, b"l")
        assert b"Ambient" in read_frame(None)
        os.write(master, b"\x1b")
        closed = read_frame(2)
        assert b"Blue hour" not in closed and b"Ambient" not in closed
        # File -> Open session creates a modal browser, not a status-only action.
        os.write(master, b"\t")
        read_frame(None)
        os.write(master, b"j\r")
        browser = read_frame(None, 0)
        assert b"Open session" in browser and b"Path: " in browser
        assert b"Directories" in browser and b"Files" in browser
        os.write(master, b"\x15tests/fixtures/tui_dialog\r")
        browser = read_frame(None, 0)
        assert b"alpha.session" in browser and b"qhjk session.session" in browser
        os.write(master, b"\x1b[106;6u")  # path -> files
        read_frame(None, 2)
        os.write(master, b"\x1b[104;6u")  # files -> directories
        read_frame(None, 1)
        os.write(master, b"jl")  # select branch, expand it
        browser = read_frame(None, 1)
        assert b"child.session" in browser and b"nested" in browser
        os.write(master, b"\x1b[108;6u")
        read_frame(None, 2)
        os.write(master, b"\x1b[107;6u")
        read_frame(None, 0)
        os.write(master, b"\x1b[106;6u\r")
        assert b"Selected:" in read_frame(2)
        # An invalid typed path keeps the dialog open, even when it contains q.
        os.write(master, b"\tj\r")
        read_frame(None, 0)
        os.write(master, b"\x15qhjk-does-not-exist.session\r")
        assert b"inaccessible" in read_frame(None, 0)
        os.write(master, b"\x15tests/fixtures/tui_dialog/qhjk session.session\r")
        assert b"Selected:" in read_frame(2)
        # Cancellation restores pane focus and small viewports remain usable.
        os.write(master, b"\tj\r")
        read_frame(None, 0)
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 6, 12, 0, 0))
        read_until(b"\x1b[0m")
        os.write(master, b"\x1b")
        read_until(b"\x1b[0m")
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
        read_frame(0)  # prior pane collapsed on resize; visible fallback is Library
        # A resize causes re-layout and a full frame with the new bottom row.
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 12, 45, 0, 0))
        read_until(b"\x1b[12;1H")
        os.write(master, b"q")
        cleanup = read_until(b"\x1b[?1049l")
        assert b"\x1b[<u" in cleanup
        assert process.wait(timeout=5) == 0
        after = termios.tcgetattr(slave)
        # macOS may set PENDIN when returning to canonical input; it is pending
        # input bookkeeping, not a raw-mode setting controlled by the program.
        pending = getattr(termios, "PENDIN", 0)
        before[3] &= ~pending
        after[3] &= ~pending
        assert after == before, f"Terminal settings were not restored: before={before!r}, after={after!r}"
        print("PASS: Open session dialog, path editing, directory browsing, modal focus, submenus, resize, terminal restoration")
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
        os.close(master)
        os.close(slave)


if __name__ == "__main__":
    main()
