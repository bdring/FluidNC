import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VFD_DIR = ROOT / "FluidNC" / "src" / "Spindles" / "VFD"


def extract_vfd_sources(text: str) -> set[str]:
    return set(re.findall(r"Spindles/VFD/([A-Za-z0-9_]+\.cpp)", text))


class BuildManifestTests(unittest.TestCase):
    def assert_vfd_sources_exist(self, manifest: Path):
        sources = extract_vfd_sources(manifest.read_text(encoding="utf-8"))
        self.assertGreater(len(sources), 0, f"{manifest} should reference at least one VFD source")
        for source in sorted(sources):
            with self.subTest(manifest=manifest.name, source=source):
                self.assertTrue((VFD_DIR / source).exists(), f"{manifest} references missing source {source}")

    def test_platformio_vfd_sources_exist(self):
        self.assert_vfd_sources_exist(ROOT / "platformio.ini")

    def test_cmake_vfd_sources_exist(self):
        self.assert_vfd_sources_exist(ROOT / "FluidNC" / "src" / "CMakeLists.txt")


if __name__ == "__main__":
    unittest.main()
