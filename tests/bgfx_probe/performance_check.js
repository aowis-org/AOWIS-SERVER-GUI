'use strict';
// Compare actual browser probe reports. This is intentionally a RELATIVE
// regression gate, not a standalone GPU benchmark: the Qt probe uses a
// 16 ms timer and is normally close to the refresh-rate ceiling.
const fs = require('node:fs');
const assert = require('node:assert/strict');

function median(values) {
    const sorted = [...values].sort((a, b) => a - b);
    const middle = Math.floor(sorted.length / 2);
    return sorted.length % 2 ? sorted[middle] : (sorted[middle - 1] + sorted[middle]) / 2;
}

function parseReports(text) {
    const reports = [];
    // The Chrome console prefixes entries with a source location and may
    // group identical messages. The JSON itself is always on one line.
    for (const line of text.split(/\r?\n/)) {
        const start = line.indexOf('{');
        const end = line.lastIndexOf('}');
        if (start < 0 || end <= start) continue;
        let report;
        try { report = JSON.parse(line.slice(start, end + 1)); }
        catch { continue; }
        if (report.kind !== 'aowis_bgfx_probe') continue;
        if (!Number.isFinite(report.fps) || report.fps <= 0 ||
            !Number.isInteger(report.width) || report.width <= 0 ||
            !Number.isInteger(report.height) || report.height <= 0 ||
            typeof report.dialog_open !== 'boolean') continue;
        reports.push(report);
    }
    return reports;
}

function compare(reports, minimumSamples, maxLossPct, allowUnverifiedStacking = false) {
    if (reports.some(report => report.stacking_ok === false)) {
        return { status: 'fail', reason: 'Live browser Qt/bgfx stacking invariant failed' };
    }
    const groups = new Map();
    for (const report of reports) {
        const size = `${report.width}x${report.height}`;
        if (!groups.has(size)) groups.set(size, { size, closed: [], open: [], samples: [] });
        const group = groups.get(size);
        group[report.dialog_open ? 'open' : 'closed'].push(report.fps);
        group.samples.push(report);
    }
    const complete = [...groups.values()].filter(group =>
        group.open.length >= minimumSamples && group.closed.length >= minimumSamples);
    if (!complete.length) {
        return {
            status: 'inconclusive',
            reason: `Need ${minimumSamples} dialog-open and ${minimumSamples} dialog-closed reports at exactly the same viewport size`,
            groups: [...groups.values()].map(group => ({
                size: group.size, closed: group.closed.length, open: group.open.length,
            })),
        };
    }
    // Use the viewport with the most useful paired measurements; never mix
    // different canvas sizes, and avoid single-frame resize/visibility spikes
    // with the median rather than the mean.
    complete.sort((a, b) =>
        Math.min(b.open.length, b.closed.length) - Math.min(a.open.length, a.closed.length)
        || b.samples.length - a.samples.length);
    const selected = complete[0];
    const closedFps = median(selected.closed);
    const openFps = median(selected.open);
    const lossPct = (closedFps - openFps) / closedFps * 100;
    const stackingVerified = selected.samples.every(sample => sample.stacking_ok === true);
    // A successful FPS comparison alone does not establish that a separately
    // composited Qt dialog is actually above bgfx. Older WASM builds omit the
    // field completely; do not silently count them as a passed stacking test.
    const status = !stackingVerified && !allowUnverifiedStacking
        ? 'inconclusive'
        : lossPct > maxLossPct ? 'fail' : 'pass';
    return {
        status,
        ...(status === 'inconclusive' ? {
            reason: 'Live stacking not verified: rebuild/redeploy the WASM probe and require stacking_ok=true in every sample'
        } : {}),
        size: selected.size,
        closed_samples: selected.closed.length,
        open_samples: selected.open.length,
        closed_median_fps: Number(closedFps.toFixed(2)),
        open_median_fps: Number(openFps.toFixed(2)),
        fps_loss_percent: Number(lossPct.toFixed(2)),
        limit_percent: maxLossPct,
        live_stacking_verified: stackingVerified,
    };
}

function selfTest() {
    const report = (fps, dialog_open, width = 1221, stacking_ok = true) =>
        ({ kind: 'aowis_bgfx_probe', fps, width, height: 938, dialog_open, stacking_ok });
    const stable = [
        report(62.5, false), report(62.55, false), report(62.6, false),
        report(62.5, true), report(62.52, true), report(62.55, true),
        report(45, false, 1782), // Resize sample: must not be pooled.
    ];
    assert.equal(compare(stable, 3, 10).status, 'pass');
    assert.equal(compare(stable, 3, 10).size, '1221x938');
    const oldBuildReports = stable.map(value => ({...value, stacking_ok: undefined}));
    assert.equal(compare(oldBuildReports, 3, 10).status, 'inconclusive');
    assert.equal(compare(oldBuildReports, 3, 10, true).status, 'pass');
    assert.equal(compare(oldBuildReports, 3, 10, true).live_stacking_verified, false);
    assert.equal(compare(stable.slice(0, 5), 3, 10).status, 'inconclusive');
    assert.equal(compare(stable.map(value => value.dialog_open
        ? { ...value, fps: 25 } : value), 3, 10).status, 'fail');
    assert.equal(compare(stable.map((value, index) => index === 0
        ? { ...value, stacking_ok: false } : value), 3, 10).status, 'fail');
    const copiedChromeLog = stable.map(value =>
        `aowis-bgfx-probe.js:8 ${JSON.stringify(value)}`).join('\n');
    assert.equal(parseReports(copiedChromeLog).length, stable.length);
    assert.equal(parseReports(`noise\n2aowis-bgfx-probe.js:8 ${JSON.stringify(stable[0])}`)
        .length, 1);
    console.log('bgfx WASM performance analyzer: OK');
}

function main(args) {
    if (args.includes('--self-test')) { selfTest(); return 0; }
    let maxLossPct = 10;
    let minimumSamples = 3;
    let allowUnverifiedStacking = false;
    let file = null;
    for (let i = 0; i < args.length; ++i) {
        if (args[i] === '--max-loss-pct' && i + 1 < args.length) {
            maxLossPct = Number(args[++i]);
        } else if (args[i] === '--min-samples' && i + 1 < args.length) {
            minimumSamples = Number(args[++i]);
        } else if (args[i] === '--allow-unverified-stacking') {
            allowUnverifiedStacking = true;
        } else if (args[i] === '--help') {
            console.log('Usage: node performance_check.js [--max-loss-pct 10] [--min-samples 3] [--allow-unverified-stacking] <browser-console.log|->');
            return 0;
        } else if (!args[i].startsWith('-') || args[i] === '-') {
            if (file !== null) throw Error('Only one input file may be specified');
            file = args[i];
        } else {
            throw Error(`Unknown argument: ${args[i]}`);
        }
    }
    if (!Number.isFinite(maxLossPct) || maxLossPct < 0 || maxLossPct > 100 ||
        !Number.isInteger(minimumSamples) || minimumSamples < 2 || !file)
        throw Error('Specify a console log, valid --max-loss-pct (0..100) and --min-samples (>=2)');
    const input = file === '-' ? fs.readFileSync(0, 'utf8') : fs.readFileSync(file, 'utf8');
    const result = compare(parseReports(input), minimumSamples, maxLossPct,
        allowUnverifiedStacking);
    console.log(JSON.stringify({ kind: 'aowis_bgfx_probe_comparison', ...result }));
    if (result.status === 'pass' && !result.live_stacking_verified)
        console.warn('WARNING: FPS comparison only; live Qt/bgfx stacking was NOT verified');
    return result.status === 'pass' ? 0 : result.status === 'fail' ? 1 : 2;
}

try { process.exitCode = main(process.argv.slice(2)); }
catch (error) { console.error(error.message); process.exitCode = 2; }
