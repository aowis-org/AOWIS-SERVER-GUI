#!/usr/bin/env python3
"""Verify that four-mode map switching retains one bgfx renderer.

Consumes AOWIS_MAP_RENDER_DIAGNOSTICS=1 output (stderr/stdout combined).
This is an end-to-end log checker, not a substitute for visual/UI tests.
"""

import argparse
import json
from pathlib import Path
import sys


class DiagnosticsError(ValueError):
    """The supplied trace does not satisfy the shared-viewport contract."""


REQUIRED = (
    "workspace_mode_switch",
    "bgfx_device_started",
    "bgfx_device_stopped",
    "bgfx_2d_scene_prepared",
)


def inspect_log(text: str, min_switches: int = 8,
                min_cache_switches: int = 4) -> dict:
    """Return a summary; raise DiagnosticsError on an invalid trace."""
    if min_switches < 1 or min_cache_switches < 0:
        raise ValueError("switch thresholds must be non-negative, with at least one switch")

    samples = []
    for lineno, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            item = json.loads(line)
        except json.JSONDecodeError:
            continue  # Other programs may write non-JSON lines.
        if not isinstance(item, dict) or item.get("kind") != "aowis_map_render_diagnostics":
            continue
        counts = item.get("counts")
        if not isinstance(counts, dict) or any(key not in counts for key in REQUIRED):
            raise DiagnosticsError(f"line {lineno}: incomplete renderer diagnostics")
        for key in REQUIRED:
            val = counts[key]
            if isinstance(val, bool) or not isinstance(val, (int, float)) or val < 0 or int(val) != val:
                raise DiagnosticsError(f"line {lineno}: invalid counter {key}")
        samples.append((lineno, counts, item))

    if not samples:
        raise DiagnosticsError("no map render diagnostics JSON snapshots found")

    previous = None
    last_prep_switches = 0
    for lineno, counts, _ in samples:
        if counts["bgfx_device_started"] > 1:
            raise DiagnosticsError(f"line {lineno}: bgfx initialized more than once")
        if counts["bgfx_device_stopped"] != 0:
            raise DiagnosticsError(f"line {lineno}: bgfx shutdown during the run")
        if previous is not None:
            for key in REQUIRED:
                if counts[key] < previous[key]:
                    raise DiagnosticsError(f"line {lineno}: {key} counter decreased")
            if counts["bgfx_2d_scene_prepared"] > previous["bgfx_2d_scene_prepared"]:
                last_prep_switches = counts["workspace_mode_switch"]
        else:
            last_prep_switches = counts["workspace_mode_switch"]
        previous = counts

    _, counts, item = samples[-1]
    switches = int(counts["workspace_mode_switch"])
    if counts["bgfx_device_started"] != 1:
        raise DiagnosticsError("the renderer did not initialize successfully")
    if switches < min_switches:
        raise DiagnosticsError(
            f"only {switches} mode switches recorded (need {min_switches})"
        )
    reused_switches = switches - int(last_prep_switches)
    if reused_switches < min_cache_switches:
        raise DiagnosticsError(
            f"only {reused_switches} mode switches after the last scene preparation "
            f"(need {min_cache_switches}); repeat mode switches after network loading"
        )

    total_ms = item.get("total_duration_ms", {}).get("workspace_mode_switch", 0)
    max_ms = item.get("max_duration_ms", {}).get("workspace_mode_switch", 0)
    return {
        "snapshots": len(samples),
        "mode_switches": switches,
        "bgfx_started": 1,
        "bgfx_stopped": 0,
        "scene_preparations": int(counts["bgfx_2d_scene_prepared"]),
        "switches_after_last_preparation": reused_switches,
        "average_switch_cpu_ms": round(total_ms / switches, 3) if switches else 0,
        "average_cpu_ms_excluding_slowest_switch": (
            round(max(0.0, total_ms - max_ms) / (switches - 1), 3)
            if switches > 1 else None
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, help="combined stdout/stderr trace file")
    parser.add_argument("--min-switches", type=int, default=8)
    parser.add_argument("--min-cache-switches", type=int, default=4)
    args = parser.parse_args()
    try:
        report = inspect_log(args.log.read_text(encoding="utf-8", errors="replace"),
                             args.min_switches, args.min_cache_switches)
    except (OSError, DiagnosticsError, ValueError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    print("PASS: persistent four-mode bgfx renderer lifecycle")
    for key, value in report.items():
        print(f"  {key}: {value}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
