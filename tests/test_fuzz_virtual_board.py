"""The fuzzer of the virtual board: it passes on the real receive path and it notices each kind of misbehaviour."""
import importlib.util
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location("fuzz_virtual_board", ROOT / "tools" / "fuzz_virtual_board.py")
fuzz = importlib.util.module_from_spec(_spec)
sys.modules["fuzz_virtual_board"] = fuzz
_spec.loader.exec_module(fuzz)


def stand_in(body: str) -> list[str]:
    """A stand-in for the virtual board: a Python program that reads the lines and prints what the body says."""
    directory = Path(tempfile.mkdtemp())
    path = directory / "board.py"
    path.write_text("import sys\n" + textwrap.dedent(body), encoding="utf-8")
    return [sys.executable, str(path)]


FRAMES = [(0x7F0, bytes.fromhex("B0071D5A"), "valid"), (0x110, b"", "valid"), (0x7F0, bytes.fromhex("B0071D5B"), "invalid")]
TX = {0x111, 0x1AF}


class CorpusTests(unittest.TestCase):
    def test_the_corpus_has_every_kind_and_the_transmit_list(self):
        frames, tx = fuzz.load_corpus()
        self.assertGreater(len(frames), 500)
        self.assertEqual({k for _, _, k in frames}, {"valid", "invalid", "random"})
        self.assertIn(0x111, tx)
        self.assertNotIn(0x7F0, tx)
        self.assertTrue(all(0 <= i <= 0x7FF and len(d) <= 8 for i, d, _ in frames))

    def test_the_extra_frames_repeat_for_a_seed_and_include_near_miss_magic(self):
        a = fuzz.extra_frames(3, 400, [0x110])
        self.assertEqual(a, fuzz.extra_frames(3, 400, [0x110]))
        self.assertTrue(any(i == 0x7F0 for i, _, _ in a))


class DetectionTests(unittest.TestCase):
    def test_a_well_behaved_stand_in_is_clean(self):
        prefix = stand_in("""
            for line in sys.stdin:
                line = line.strip()
                if line == 't7F04B0071D5A':
                    print('X')
                elif line == 't1100':
                    print('t1112AABB')
        """)
        self.assertEqual(fuzz.run_once(prefix, 0, 0, FRAMES, TX), [])

    def test_a_crash_is_reported(self):
        self.assertTrue(any("stopped with status" in p for p in fuzz.run_once(stand_in("sys.exit(134)"), 0, 0, FRAMES, TX)))

    def test_a_reset_without_the_magic_is_reported(self):
        prefix = stand_in("""
            for line in sys.stdin:
                if line.strip().startswith('t7F0'):
                    print('X')
        """)
        self.assertTrue(any("reset" in p for p in fuzz.run_once(prefix, 0, 0, FRAMES, TX)))

    def test_no_reset_on_the_magic_is_reported(self):
        self.assertTrue(any("reset" in p for p in fuzz.run_once(stand_in("sys.stdin.read()"), 0, 0, FRAMES, TX)))

    def test_a_malformed_line_is_reported(self):
        prefix = stand_in("""
            sys.stdin.read()
            print('X')
            print('t11')
        """)
        self.assertTrue(any("malformed" in p for p in fuzz.run_once(prefix, 0, 0, FRAMES, TX)))

    def test_an_identifier_the_board_may_not_send_is_reported(self):
        prefix = stand_in("""
            sys.stdin.read()
            print('X')
            print('t1200')
        """)
        self.assertTrue(any("not an identifier it may send" in p for p in fuzz.run_once(prefix, 0, 0, FRAMES, TX)))

    def test_a_length_that_disagrees_with_the_data_is_reported(self):
        prefix = stand_in("""
            sys.stdin.read()
            print('X')
            print('t1113AABB')
        """)
        self.assertTrue(any("disagree" in p for p in fuzz.run_once(prefix, 0, 0, FRAMES, TX)))


class RealBoardTests(unittest.TestCase):
    def test_the_real_receive_path_survives_the_corpus(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build" if (ROOT / "build").is_dir() else None) as directory:
            status = fuzz.main(["--build-dir", directory, "--extra", "300"])
        if status == 2:
            self.skipTest("no host C compiler")
        self.assertEqual(status, 0)


if __name__ == "__main__":
    unittest.main()
