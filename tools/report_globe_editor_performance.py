#!/usr/bin/env python3
"""Report measured 3D Editor icon CPU costs from cumulative GUI diagnostics.

Run the application with AOWIS_MAP_RENDER_DIAGNOSTICS=1, capture stdout and
stderr together, navigate in 3D Distribution Editor, then supply that log.
The optional synthetic benchmark is NOT a substitute for this runtime trace.
"""

import argparse
import json
from pathlib import Path
import statistics
import sys

KIND = "aowis_map_render_diagnostics"
COUNT_KEYS = (
    "frames", "globe_editor_icon_frame", "globe_editor_icon_anchors",
    "globe_editor_icon_candidates", "globe_editor_icon_visible",
    "globe_editor_icon_index_build", "globe_editor_icon_index_query",
    "globe_editor_icon_projection", "globe_editor_icon_draw",
)
TERRAIN_COUNT_KEYS = (
    "globe_terrain_height_cache_hit", "globe_terrain_height_sample",
    "globe_terrain_height_unavailable_reuse", "globe_terrain_height_eviction",
)
DURATION_KEYS = (
    "frames", "globe_editor_icon_frame", "globe_editor_icon_index_query",
    "globe_editor_icon_projection", "globe_editor_icon_draw",
)


class PerformanceError(ValueError):
    """The trace is incomplete or inconsistent."""


def _number(value: object, key: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise PerformanceError(f"invalid number: {key}")
    result = float(value)
    if not (float("-inf") < result < float("inf")) or result < 0:
        raise PerformanceError(f"invalid nonnegative number: {key}")
    return result


def read_snapshots(text: str) -> list[dict]:
    snapshots = []
    for number, line in enumerate(text.splitlines(), 1):
        if not line.lstrip().startswith("{"):
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        if not isinstance(record, dict) or record.get("kind") != KIND:
            continue
        counts = record.get("counts")
        durations = record.get("total_duration_ms")
        if not isinstance(counts, dict) or not isinstance(durations, dict):
            raise PerformanceError(f"line {number}: invalid map diagnostics object")
        if any(key not in counts for key in COUNT_KEYS):
            raise PerformanceError(
                f"line {number}: missing Stage 10 counters (rebuild GUI with Stage 10 patch)")
        count_values = {key: _number(counts[key], key) for key in COUNT_KEYS}
        for key, value in count_values.items():
            if int(value) != value:
                raise PerformanceError(f"line {number}: fractional count {key}")
        duration_values = {key: _number(durations.get(key, 0), key)
                           for key in DURATION_KEYS}
        terrain_counts = {key: _number(counts[key], key)
                          for key in TERRAIN_COUNT_KEYS if key in counts}
        if any(int(value) != value for value in terrain_counts.values()):
            raise PerformanceError(f"line {number}: fractional terrain counter")
        snapshots.append({"line": number, "counts": count_values,
                          "terrain_counts": terrain_counts,
                          "durations": duration_values})
    if len(snapshots) < 2:
        raise PerformanceError("need at least two map diagnostics snapshots")
    return snapshots


def analyze(text: str, min_icon_frames: int = 1, min_anchors: int = 0) -> dict:
    if min_icon_frames < 1 or min_anchors < 0:
        raise PerformanceError("minimum thresholds must be nonnegative")
    snapshots = read_snapshots(text)
    windows = []
    for previous, current in zip(snapshots, snapshots[1:]):
        count_delta = {key: current["counts"][key] - previous["counts"][key]
                       for key in COUNT_KEYS}
        duration_delta = {key: current["durations"][key] - previous["durations"][key]
                          for key in DURATION_KEYS}
        if any(value < 0 for value in count_delta.values()) or any(
                value < -0.000001 for value in duration_delta.values()):
            raise PerformanceError(f"line {current['line']}: decreasing cumulative counters")
        icon_frames = int(count_delta["globe_editor_icon_frame"])
        if not icon_frames:
            continue
        if any(count_delta[key] != icon_frames for key in (
                "globe_editor_icon_index_query", "globe_editor_icon_projection",
                "globe_editor_icon_draw")):
            raise PerformanceError(
                f"line {current['line']}: inconsistent Stage 10 icon scope counts")
        anchors = count_delta["globe_editor_icon_anchors"] / icon_frames
        if anchors < min_anchors:
            continue
        candidate_count = count_delta["globe_editor_icon_candidates"]
        if candidate_count > count_delta["globe_editor_icon_anchors"]:
            raise PerformanceError(f"line {current['line']}: candidates exceed anchors")
        if count_delta["globe_editor_icon_visible"] > candidate_count:
            raise PerformanceError(f"line {current['line']}: visible icons exceed candidates")
        terrain_delta = {}
        for key in TERRAIN_COUNT_KEYS:
            if key in previous["terrain_counts"] and key in current["terrain_counts"]:
                value = current["terrain_counts"][key] - previous["terrain_counts"][key]
                if value < 0:
                    raise PerformanceError(f"line {current['line']}: decreasing {key}")
                terrain_delta[key] = value
        windows.append({
            "terrain_delta": terrain_delta,
            "icon_frames": icon_frames,
            "presented_frames": int(count_delta["frames"]),
            "anchors": count_delta["globe_editor_icon_anchors"],
            "candidates": candidate_count,
            "visible": count_delta["globe_editor_icon_visible"],
            "geometry_index_rebuilds": int(count_delta["globe_editor_icon_index_build"]),
            "frame_cpu_ms": duration_delta["frames"],
            "icon_cpu_ms": duration_delta["globe_editor_icon_frame"],
            "query_cpu_ms": duration_delta["globe_editor_icon_index_query"],
            "projection_cpu_ms": duration_delta["globe_editor_icon_projection"],
            "drawing_cpu_ms": duration_delta["globe_editor_icon_draw"],
            "average_icon_cpu_ms": duration_delta["globe_editor_icon_frame"] / icon_frames,
        })
    frames = sum(window["icon_frames"] for window in windows)
    if frames < min_icon_frames:
        raise PerformanceError(
            f"only {frames} 3D Editor icon frames recorded (need {min_icon_frames}); "
            "open Distribution Editor in 3D and move the camera")
    sum_value = lambda key: sum(window[key] for window in windows)
    anchors = sum_value("anchors")
    candidates = sum_value("candidates")
    samples = sorted(window["average_icon_cpu_ms"] for window in windows)
    result = {
        "kind": "aowis_real_globe_editor_icon_cpu_report",
        "windows": len(windows),
        "editor_icon_frames": frames,
        "presented_frames_in_windows": sum_value("presented_frames"),
        "mean_icon_anchors_per_frame": round(anchors / frames, 2),
        "mean_candidates_per_frame": round(candidates / frames, 2),
        "mean_visible_per_frame": round(sum_value("visible") / frames, 2),
        "candidate_fraction": round(candidates / anchors, 6) if anchors else 0,
        "index_rebuilds_in_windows": sum_value("geometry_index_rebuilds"),
        "mean_icon_cpu_ms_per_frame": round(sum_value("icon_cpu_ms") / frames, 4),
        "mean_index_query_cpu_ms_per_frame": round(sum_value("query_cpu_ms") / frames, 4),
        "mean_projection_cpu_ms_per_frame": round(
            sum_value("projection_cpu_ms") / frames, 4),
        "mean_sort_and_draw_cpu_ms_per_frame": round(
            sum_value("drawing_cpu_ms") / frames, 4),
        "mean_bgfx_submit_cpu_ms_per_presented_frame": round(
            sum_value("frame_cpu_ms") / sum_value("presented_frames"), 4)
        if sum_value("presented_frames") else None,
        "highest_window_mean_icon_cpu_ms": round(max(samples), 4),
        "median_window_mean_icon_cpu_ms": round(statistics.median(samples), 4),
        "notes": [
            "Measured with the actual GUI and terrain-aware projector (CPU only, not GPU timing)",
            "Window means are averages between diagnostics snapshots; they are not per-frame p95",
            "Mode-switch-only snapshots and zero-icon intervals are excluded",
            "Times include diagnostics instrumentation overhead when enabled",
        ],
    }
    for key, label in (
            ("globe_terrain_height_cache_hit", "mean_terrain_height_hits_per_icon_frame"),
            ("globe_terrain_height_sample", "mean_terrain_height_samples_per_icon_frame"),
            ("globe_terrain_height_unavailable_reuse", "mean_unavailable_retries_avoided_per_icon_frame"),
            ("globe_terrain_height_eviction", "terrain_height_cache_evictions")):
        if all(key in window["terrain_delta"] for window in windows):
            count = sum(window["terrain_delta"][key] for window in windows)
            result[label] = (round(count / frames, 4) if label.startswith("mean_") else count)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="combined stdout/stderr GUI trace")
    parser.add_argument("--min-icon-frames", type=int, default=1)
    parser.add_argument("--min-anchors", type=int, default=0,
                        help="ignore sample windows with fewer anchors per icon frame")
    parser.add_argument("--json", action="store_true", help="emit JSON for comparisons")
    args = parser.parse_args()
    try:
        result = analyze(args.log.read_text(encoding="utf-8", errors="replace"),
                         min_icon_frames=args.min_icon_frames, min_anchors=args.min_anchors)
    except (OSError, PerformanceError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print("PASS: 3D Editor CPU diagnostic report")
        for key, value in result.items():
            if key != "notes":
                print(f"  {key}: {value}")
        for note in result["notes"]:
            print(f"  note: {note}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
