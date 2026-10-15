'use strict';
// Node-only structural test for the browser canvas stacking contract.
// GPU timing is measured in the browser probe, not asserted in this test.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

class Element {
    constructor(name) {
        this.name = name;
        this.children = [];
        this.parentElement = null;
        this.id = '';
        this.events = new Map();
        this.bounds = { left: 0, top: 0, width: 0, height: 0 };
        this.style = {
            setProperty(key, value) { this[key] = value; this[key.replace(/-([a-z])/g, (_, c) => c.toUpperCase())] = value; }
        };
    }
    appendChild(child) {
        if (child.parentElement)
            child.parentElement.children = child.parentElement.children.filter(item => item !== child);
        child.parentElement = this;
        this.children.push(child);
        return child;
    }
    remove() {
        if (this.parentElement)
            this.parentElement.children = this.parentElement.children.filter(item => item !== this);
        this.parentElement = null;
    }
    getBoundingClientRect() { return this.bounds; }
    addEventListener(name, fn) { this.events.set(name, fn); }
    removeEventListener(name) { this.events.delete(name); }
    get isConnected() { return Boolean(this.parentElement); }
    querySelector(selector) {
        return this.children.find(child =>
            selector === 'canvas.qt-window-canvas' && child.name === 'qt-window-canvas') || null;
    }
    querySelectorAll(selector) {
        return selector === '.qt-decorated-window'
            ? this.children.filter(child => child.name === 'qt-decorated-window')
            : [];
    }
}

const body = new Element('body');
const qtScreen = new Element('qt-screen');
qtScreen.bounds = { left: 14, top: 23, width: 1200, height: 800 };
const makeWindow = (width, height) => {
    const wrapper = new Element('qt-decorated-window');
    const canvas = new Element('qt-window-canvas');
    canvas.bounds = { left: 0, top: 0, width, height };
    wrapper.appendChild(canvas);
    qtScreen.appendChild(wrapper);
    return wrapper;
};
const mainWindow = makeWindow(900, 600);
const dialog = makeWindow(360, 240);
const shadowHost = { shadowRoot: { querySelector: selector =>
    selector === '.qt-screen' ? qtScreen : null } };
const queue = [];
const fakeDocument = {
    body,
    createElement: name => new Element(name),
    getElementById: id => id === 'qt-shadow-container' ? shadowHost : null,
};
const fakeWindow = { devicePixelRatio: 2 };
const context = vm.createContext({
    window: fakeWindow,
    document: fakeDocument,
    MutationObserver: class { observe() {} disconnect() {} },
    requestAnimationFrame: callback => queue.push(callback),
    getComputedStyle: element => element.style,
});
const source = fs.readFileSync(path.resolve(__dirname,
    '../../tools/qt-emscripten/bgfx-probe-stack.js'), 'utf8');
vm.runInContext(source, context, { filename: 'bgfx-probe-stack.js' });
const bridge = fakeWindow.aowisBgfxProbe;
assert.equal(bridge.prepare(), true);
const canvas = bridge.canvas();
assert.equal(canvas.parentElement, null);
assert.equal(canvas.id, 'aowis-bgfx-probe-canvas');
assert.equal(bridge.attach(), true);
assert.equal(canvas.parentElement, qtScreen);
bridge.geometry(40, 60, 600, 320, true);
while (queue.length)
    queue.shift()();
assert.equal(mainWindow.style.zIndex, '10');
assert.equal(canvas.style.zIndex, '20');
assert.equal(dialog.style.zIndex, '30');
assert.equal(canvas.style.left, '26px');
assert.equal(canvas.style.top, '37px');
assert.equal(canvas.width, 1200);
assert.equal(canvas.height, 640);
const inspection = bridge.inspect();
assert.equal(inspection.ok, true);
assert.equal(inspection.mainWindowZ, 10);
assert.equal(inspection.transientCount, 1);
assert.equal(inspection.transientZ[0], 30);
// The runtime inspection must reject reversed stacking, even if the canvas
// itself remains at its expected z-index.
dialog.style.zIndex = '15';
assert.equal(bridge.inspect().ok, false);
dialog.style.zIndex = '30';
mainWindow.style.zIndex = '25';
assert.equal(bridge.inspect().ok, false);
mainWindow.style.zIndex = '10';
assert.equal(bridge.inspect().ok, true);
bridge.remove();
assert.equal(canvas.parentElement, null);
console.log('bgfx browser stacking contract: OK');
