'use strict';
// Production canvas DOM/lifecycle regression. Does not assert GPU initialization.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

class Element {
    constructor(name) {
        this.name = name;
        this.parentElement = null;
        this.children = [];
        this.events = new Map();
        this.bounds = { left: 0, top: 0, width: 0, height: 0 };
        this.style = {
            setProperty(key, value) { this[key.replace(/-([a-z])/g, (_, c) => c.toUpperCase())] = value; }
        };
    }
    appendChild(child) {
        child.remove();
        child.parentElement = this;
        this.children.push(child);
    }
    remove() {
        if (this.parentElement)
            this.parentElement.children = this.parentElement.children.filter(c => c !== this);
        this.parentElement = null;
    }
    getBoundingClientRect() { return this.bounds; }
    addEventListener(name, listener) { this.events.set(name, listener); }
    removeEventListener(name) { this.events.delete(name); }
    get isConnected() { return this.parentElement !== null; }
    querySelector(selector) {
        return selector === 'canvas.qt-window-canvas'
            ? this.children.find(c => c.name === 'qt-window-canvas') : null;
    }
    querySelectorAll(selector) {
        return selector === '.qt-decorated-window'
            ? this.children.filter(c => c.name === 'qt-decorated-window') : [];
    }
}
const screen = new Element('screen');
screen.bounds = { left: 10, top: 20 };
function addWindow(w, h) {
    const wrapper = new Element('qt-decorated-window');
    const backing = new Element('qt-window-canvas');
    backing.bounds = { left: 0, top: 0, width: w, height: h };
    wrapper.appendChild(backing);
    screen.appendChild(wrapper);
    return wrapper;
}
const main = addWindow(800, 650);
const popup = addWindow(300, 160);
const queue = [];
const context = vm.createContext({
    document: {
        createElement: name => new Element(name),
        getElementById: id => id === 'qt-shadow-container'
            ? { shadowRoot: { querySelector: () => screen } } : null
    },
    window: { devicePixelRatio: 2 },
    MutationObserver: class { observe() {} disconnect() {} },
    requestAnimationFrame: callback => queue.push(callback),
});
const source = fs.readFileSync(path.resolve(__dirname,
    '../tools/qt-emscripten/aowis-bgfx-map.js'), 'utf8');
vm.runInContext(source, context, { filename: 'aowis-bgfx-map.js' });
const bridge = context.window.aowisBgfxMap;
assert.equal(bridge.prepare(), true);
assert.equal(bridge.attach(), true);
const canvas = bridge.canvas();
assert.equal(canvas.parentElement, screen);
assert.equal(canvas.style.pointerEvents, 'none');
bridge.geometry(40, 60, 600, 300, true);
while (queue.length) queue.shift()();
assert.equal(main.style.zIndex, '10');
assert.equal(canvas.style.zIndex, '20');
assert.equal(popup.style.zIndex, '30');
assert.equal(canvas.style.display, 'block');
assert.equal(canvas.style.left, '30px');
assert.equal(canvas.style.top, '40px');
assert.equal(canvas.width, 1200);
assert.equal(canvas.height, 600);
// Unchanged geometry must not reset the underlying WebGL canvas.
canvas.width = 1200;
bridge.geometry(40, 60, 600, 300, true);
assert.equal(canvas.width, 1200);
bridge.geometry(41, 63, 600, 300, false);
while (queue.length) queue.shift()();
assert.equal(canvas.style.left, '31px');
assert.equal(canvas.style.display, 'none');
bridge.remove();
assert.equal(canvas.parentElement, null);
console.log('bgfx production map canvas contract: OK');
