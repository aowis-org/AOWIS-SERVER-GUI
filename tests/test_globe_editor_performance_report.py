#!/usr/bin/env python3
"""Regression tests for interpreting real GUI Stage 10 cumulative traces."""

import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from report_globe_editor_performance import PerformanceError, analyze  # noqa: E402


def row(frames: int, anchors: int, candidates: int, visible: int,
        icon_cpu: float, build: int = 1) -> str:
    keys = {"frames": frames, "globe_editor_icon_frame": frames,
            "globe_editor_icon_anchors": anchors,
            "globe_editor_icon_candidates": candidates,
            "globe_editor_icon_visible": visible,
            "globe_editor_icon_index_build": build,
            "globe_editor_icon_index_query": frames,
            "globe_editor_icon_projection": frames,
            "globe_editor_icon_draw": frames}
    times = {"frames": frames * 10.0,
             "globe_editor_icon_frame": icon_cpu,
             "globe_editor_icon_index_query": icon_cpu * 0.1,
             "globe_editor_icon_projection": icon_cpu * 0.6,
             "globe_editor_icon_draw": icon_cpu * 0.3}
    return json.dumps({"kind": "aowis_map_render_diagnostics", "counts": keys,
                       "total_duration_ms": times})


class PerformanceReportTest(unittest.TestCase):
    def test_averages_use_deltas_and_weighted_frames(self):
        text = "\n".join([row(0, 0, 0, 0, 0), row(10, 100000, 2000, 1200, 50),
                          row(30, 300000, 10000, 5000, 110)])
        summary = analyze(text, min_icon_frames=30)
        self.assertEqual(summary["editor_icon_frames"], 30)
        self.assertEqual(summary["mean_icon_anchors_per_frame"], 10000)
        self.assertEqual(summary["mean_candidates_per_frame"], 333.33)
        self.assertEqual(summary["index_rebuilds_in_windows"], 0)
        self.assertEqual(summary["mean_icon_cpu_ms_per_frame"], 3.6667)
        self.assertEqual(summary["mean_projection_cpu_ms_per_frame"], 2.2)

    def test_duplicate_and_idle_snapshots_skipped(self):
        initial = row(0, 0, 0, 0, 0)
        later = row(4, 40, 10, 5, 20)
        result = analyze("\n".join([initial, initial, later, later]))
        self.assertEqual(result["windows"], 1)
        self.assertEqual(result["mean_icon_cpu_ms_per_frame"], 5)

    def test_incomplete_stage10_counters_rejected(self):
        with self.assertRaisesRegex(PerformanceError, "Stage 10 counters"):
            analyze(row(0, 0, 0, 0, 0).replace('"globe_editor_icon_frame": 0, ', ""))

    def test_counter_reset_rejected(self):
        with self.assertRaisesRegex(PerformanceError, "decreasing"):
            analyze("\n".join([row(10, 100, 30, 20, 50), row(2, 20, 5, 4, 8)]))

    def test_candidate_and_visible_invariants(self):
        with self.assertRaisesRegex(PerformanceError, "candidates exceed"):
            analyze("\n".join([row(0, 0, 0, 0, 0), row(3, 30, 31, 6, 4)]))
        with self.assertRaisesRegex(PerformanceError, "visible icons exceed"):
            analyze("\n".join([row(0, 0, 0, 0, 0), row(3, 30, 10, 11, 4)]))

    def test_minimum_anchors_and_frames(self):
        text = "\n".join([row(0, 0, 0, 0, 0), row(5, 500, 100, 80, 6),
                          row(15, 100500, 2100, 980, 26)])
        result = analyze(text, min_anchors=10000, min_icon_frames=10)
        self.assertEqual(result["editor_icon_frames"], 10)
        with self.assertRaisesRegex(PerformanceError, "only 10"):
            analyze(text, min_anchors=10000, min_icon_frames=11)

    def test_stage12_terrain_stats_and_legacy_logs(self):
        first = json.loads(row(0, 0, 0, 0, 0))
        last = json.loads(row(20, 40000, 20000, 12000, 60))
        fields = ("globe_terrain_height_cache_hit", "globe_terrain_height_sample",
                  "globe_terrain_height_unavailable_reuse", "globe_terrain_height_eviction")
        for field, value in zip(fields, (200, 200, 0, 0)):
            first["counts"][field] = value
        for field, value in zip(fields, (1200, 500, 700, 3)):
            last["counts"][field] = value
        result = analyze("\n".join((json.dumps(first), json.dumps(last))))
        self.assertEqual(result["mean_terrain_height_hits_per_icon_frame"], 50)
        self.assertEqual(result["mean_terrain_height_samples_per_icon_frame"], 15)
        self.assertEqual(result["mean_unavailable_retries_avoided_per_icon_frame"], 35)
        self.assertEqual(result["terrain_height_cache_evictions"], 3)
        legacy = analyze("\n".join((row(0, 0, 0, 0, 0), row(20, 200, 100, 50, 60))))
        self.assertNotIn("mean_unavailable_retries_avoided_per_icon_frame", legacy)

    def test_stage12_decreasing_terrain_count_rejected(self):
        first = json.loads(row(0, 0, 0, 0, 0))
        last = json.loads(row(10, 100, 50, 40, 30))
        first["counts"]["globe_terrain_height_sample"] = 30
        last["counts"]["globe_terrain_height_sample"] = 29
        with self.assertRaisesRegex(PerformanceError, "decreasing globe_terrain_height_sample"):
            analyze("\n".join((json.dumps(first), json.dumps(last))))

    def test_empty_and_no_icon_frames(self):
        with self.assertRaisesRegex(PerformanceError, "at least two"):
            analyze("not diagnostics")
        with self.assertRaisesRegex(PerformanceError, "only 0"):
            analyze("\n".join([row(0, 0, 0, 0, 0)] * 2))

    def test_scope_counts_must_match_icon_frames(self):
        text = "\n".join([row(0, 0, 0, 0, 0),
                          row(3, 30, 10, 6, 10).replace(
                              '"globe_editor_icon_draw": 3', '"globe_editor_icon_draw": 2')])
        with self.assertRaisesRegex(PerformanceError, "inconsistent"):
            analyze(text)


if __name__ == "__main__":
    unittest.main()
