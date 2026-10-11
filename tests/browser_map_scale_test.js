"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const source = fs.readFileSync(path.join(__dirname,
    "../tools/qt-emscripten/aowis-browser-map.js"), "utf8");
// Expose closure state only in the test VM; production API stays unchanged.
const context = {window: {devicePixelRatio: 1}, navigator: {hardwareConcurrency: 4}};
vm.createContext(context);
vm.runInContext(source.replace("    window.aowisBrowserMap = {",
    "    window.testState = state;\n    window.aowisBrowserMap = {"), context);
const state = context.window.testState;
const api = context.window.aowisBrowserMap;
Object.assign(state, {activeOwner: 1, initialized: true, width: 800,
    scaleControl: {style: {}}, scaleBar: {style: {}}, scaleLabel: {}});
const metersPerPixel = 2 * Math.PI * 6378137 / (256 * 2 ** state.zoom);
const metric = JSON.stringify([{meters: 20, label: "20 m"}, {meters: 50, label: "50 m"}]);
const imperial = JSON.stringify([{meters: 30.48, label: "100 ft"}, {meters: 60.96, label: "200 ft"}]);
const unchanged = () => JSON.stringify(api.getViewState());
const before = unchanged();
api.setScaleChoices(1, metric);
assert.equal(state.scaleLabel.textContent, "50 m");
assert.equal(state.scaleBar.style.width, Math.round(50 / metersPerPixel) + "px");
api.setScaleChoices(2, imperial);
assert.equal(state.scaleLabel.textContent, "50 m");
api.setScaleChoices(1, imperial);
assert.equal(state.scaleLabel.textContent, "200 ft");
assert.equal(state.scaleBar.style.width, Math.round(60.96 / metersPerPixel) + "px");
assert.equal(unchanged(), before);
assert.equal(state.tiles.size, 0);
assert.equal(state.renderPending, false);
for (let i = 0; i < 1000; ++i) {
    api.setScaleChoices(1, metric);
    api.setScaleChoices(1, imperial);
}
assert.equal(unchanged(), before);
state.activeOwner = 2;
api.setScaleChoices(2, metric);
assert.equal(state.scaleLabel.textContent, "50 m");
api.setScaleChoices(2, "[]");
assert.equal(state.scaleControl.style.display, "none");
assert.throws(() => api.setScaleChoices(2, '[{"meters":-1,"label":"bad"}]'));
api.setScaleChoices(2, JSON.stringify([{meters: 30.48, label: "100 localized"}]));
assert.equal(state.scaleLabel.textContent, "100 localized");
console.log("Browser scale profile, width, owner and view invariants passed");
