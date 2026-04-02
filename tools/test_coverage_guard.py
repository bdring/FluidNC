import json
import tempfile
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import coverage_guard


def run_guard(tmp_path: Path, summary_obj, gaps_obj):
    summary_path = tmp_path / "coverage-summary.json"
    gaps_path = tmp_path / "coverage-gaps.json"
    summary_path.write_text(json.dumps(summary_obj), encoding="utf-8")
    gaps_path.write_text(json.dumps(gaps_obj), encoding="utf-8")
    return coverage_guard.main_with_paths(summary_path, gaps_path)


def passing_summary():
    files = []
    for filename, threshold in coverage_guard.CRITICAL_MIN_LINE_COVERAGE.items():
        files.append({"filename": filename, "line_percent": threshold + 1.0})
    return {"files": files}


def passing_gaps():
    return {
        "active_host_files": {
            "called_percent_of_total": coverage_guard.MIN_ACTIVE_HOST_CALLED_PERCENT + 1.0
        }
    }


class CoverageGuardTests(unittest.TestCase):
    def _tmp_path(self):
        tmpdir = tempfile.TemporaryDirectory()
        self.addCleanup(tmpdir.cleanup)
        return Path(tmpdir.name)

    def test_missing_summary_returns_2(self):
        tmp_path = self._tmp_path()
        gaps_path = tmp_path / "coverage-gaps.json"
        gaps_path.write_text(json.dumps(passing_gaps()), encoding="utf-8")
        result = coverage_guard.main_with_paths(tmp_path / "missing-summary.json", gaps_path)
        self.assertEqual(result, 2)

    def test_invalid_json_returns_2(self):
        tmp_path = self._tmp_path()
        summary_path = tmp_path / "coverage-summary.json"
        gaps_path = tmp_path / "coverage-gaps.json"
        summary_path.write_text("{invalid", encoding="utf-8")
        gaps_path.write_text(json.dumps(passing_gaps()), encoding="utf-8")
        result = coverage_guard.main_with_paths(summary_path, gaps_path)
        self.assertEqual(result, 2)

    def test_malformed_structure_returns_2(self):
        tmp_path = self._tmp_path()
        result = run_guard(tmp_path, {"files": {}}, passing_gaps())
        self.assertEqual(result, 2)

    def test_guard_passes_when_thresholds_met(self):
        tmp_path = self._tmp_path()
        result = run_guard(tmp_path, passing_summary(), passing_gaps())
        self.assertEqual(result, 0)

    def test_guard_fails_when_critical_file_drops_below_threshold(self):
        tmp_path = self._tmp_path()
        summary = passing_summary()
        summary["files"][0]["line_percent"] = 0.0
        result = run_guard(tmp_path, summary, passing_gaps())
        self.assertEqual(result, 1)

    def test_guard_fails_when_active_host_called_percent_drops(self):
        tmp_path = self._tmp_path()
        gaps = passing_gaps()
        gaps["active_host_files"]["called_percent_of_total"] = 0.0
        result = run_guard(tmp_path, passing_summary(), gaps)
        self.assertEqual(result, 1)


if __name__ == "__main__":
    unittest.main()
