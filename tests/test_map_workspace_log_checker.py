#!/usr/bin/env python3
"""Qt-independent tests for map renderer diagnostics acceptance."""

import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_map_workspace_diagnostics import DiagnosticsError, inspect_log


def snapshot(switches, starts=1, stops=0, prepares=3):
    return json.dumps({
        "kind": "aowis_map_render_diagnostics",
        "counts": {
            "workspace_mode_switch": switches,
            "bgfx_device_started": starts,
            "bgfx_device_stopped": stops,
            "bgfx_2d_scene_prepared": prepares,
        },
        "total_duration_ms": {"workspace_mode_switch": 13.5 * switches},
        "max_duration_ms": {"workspace_mode_switch": 100.0},
    })


class DiagnosticsCheckerTests(unittest.TestCase):
    def valid_log(self):
        return "Starting bgfx\n" + "\n".join([
            snapshot(0, prepares=2), snapshot(1, prepares=3),
            snapshot(3, prepares=4), snapshot(5, prepares=5),
            snapshot(9, prepares=5), snapshot(14, prepares=5),
        ]) + "\n"

    def test_accepts_stable_shared_renderer(self):
        result = inspect_log(self.valid_log())
        self.assertEqual(result["mode_switches"], 14)
        self.assertEqual(result["switches_after_last_preparation"], 9)
        self.assertEqual(result["bgfx_started"], 1)

    def test_main_navigation_only_switches_retain_renderer(self):
        # The Maps tab may be left and re-entered without changing its map
        # mode. A stable GPU lifecycle must still pass with zero mode changes.
        trace = snapshot(0) + "\n" + snapshot(0) + "\n"
        result = inspect_log(trace, min_switches=0, min_cache_switches=0)
        self.assertEqual(result["mode_switches"], 0)
        self.assertEqual(result["bgfx_started"], 1)
        self.assertEqual(result["bgfx_stopped"], 0)

    def test_main_navigation_only_switches_detect_restart(self):
        trace = snapshot(0) + "\n" + snapshot(0, starts=2, stops=1) + "\n"
        with self.assertRaisesRegex(DiagnosticsError, "initialized more than once"):
            inspect_log(trace, min_switches=0, min_cache_switches=0)

    def test_rejects_device_restart(self):
        with self.assertRaisesRegex(DiagnosticsError, "initialized more than once"):
            inspect_log(self.valid_log() + snapshot(15, starts=2, stops=1))

    def test_rejects_any_shutdown(self):
        with self.assertRaisesRegex(DiagnosticsError, "shutdown"):
            inspect_log(self.valid_log() + snapshot(15, stops=1))

    def test_rejects_insufficient_cache_reuse(self):
        with self.assertRaisesRegex(DiagnosticsError, "after the last scene preparation"):
            inspect_log(self.valid_log() + snapshot(15, prepares=6))

    def test_rejects_decreasing_counter(self):
        with self.assertRaisesRegex(DiagnosticsError, "counter decreased"):
            inspect_log(self.valid_log() + snapshot(12, prepares=5))

    def test_rejects_insufficient_switches(self):
        with self.assertRaisesRegex(DiagnosticsError, "only 5 mode switches"):
            inspect_log(snapshot(5), min_switches=8)

    def test_rejects_missing_diagnostics(self):
        with self.assertRaisesRegex(DiagnosticsError, "no map render diagnostics"):
            inspect_log("Loaded GUI configuration\n")

    def test_rejects_missing_counters(self):
        with self.assertRaisesRegex(DiagnosticsError, "incomplete"):
            inspect_log('{"kind":"aowis_map_render_diagnostics","counts":{}}')

    def test_ignores_unrelated_output(self):
        result = inspect_log("{not JSON}\n" + self.valid_log() + "Other logger output")
        self.assertEqual(result["mode_switches"], 14)


if __name__ == "__main__":
    unittest.main()
