/* Experimental bgfx WebAssembly probe. No ordinary Qt widget overlays the
 * canvas: it is inserted into Qt's shadow-root screen and receives z-index 20,
 * between the main Qt window (10) and top-level dialogs (30+).
 *
 * The canvas is created directly in Qt's shadow-root screen. Emscripten's
 * specialHTMLTargets alias, installed by the probe C++ EM_JS bridge, lets
 * bgfx resolve the canvas during init AND later framebuffer resizing.
 */
(function () {
    "use strict";
    const state = {
        canvas: null,
        screen: null,
        observer: null,
        requested: null,
        pending: false
    };

    function qtScreen() {
        const host = document.getElementById("qt-shadow-container");
        return host?.shadowRoot?.querySelector(".qt-screen") || null;
    }

    function stack() {
        state.pending = false;
        if (!state.canvas || !state.screen)
            return;
        const windows = Array.from(state.screen.querySelectorAll(".qt-decorated-window"));
        const ranked = windows.map(element => ({
            element,
            canvas: element.querySelector("canvas.qt-window-canvas"),
        }));
        ranked.sort((a, b) => {
            const area = element => {
                const bounds = element?.getBoundingClientRect();
                return bounds ? bounds.width * bounds.height : 0;
            };
            return area(b.canvas) - area(a.canvas);
        });
        const mainWindow = ranked[0]?.element;
        if (!mainWindow) {
            state.canvas.style.display = "none";
            return;
        }
        mainWindow.style.setProperty("z-index", "10", "important");
        state.canvas.style.setProperty("z-index", "20", "important");
        const transientWindows = ranked.slice(1).map(item => item.element);
        transientWindows.forEach((element, index) => {
            element.style.setProperty("z-index", String(30 + index), "important");
        });
        if (state.requested)
            state.canvas.style.display = state.requested.visible ? "block" : "none";
    }

    function scheduleStack() {
        if (state.pending)
            return;
        state.pending = true;
        requestAnimationFrame(stack);
    }

    window.aowisBgfxProbe = {
        canvas() { return state.canvas; },
        prepare() {
            if (state.canvas)
                return true;
            const canvas = document.createElement("canvas");
            canvas.id = "aowis-bgfx-probe-canvas";
            canvas.style.position = "absolute";
            canvas.style.display = "none";
            canvas.style.background = "#16324a";
            canvas.style.touchAction = "none";
            state.canvas = canvas;
            return true;
        },
        attach() {
            const screen = qtScreen();
            if (!screen || !state.canvas)
                return false;
            state.screen = screen;
            state.canvas.style.position = "absolute";
            state.canvas.style.pointerEvents = "auto";
            screen.appendChild(state.canvas);
            state.observer = new MutationObserver(scheduleStack);
            state.observer.observe(screen, { childList: true });
            screen.addEventListener("focusin", scheduleStack, true);
            scheduleStack();
            return true;
        },
        geometry(x, y, w, h, visible) {
            if (!state.canvas || !state.screen)
                return;
            const screenBounds = state.screen.getBoundingClientRect();
            state.requested = { visible: Boolean(visible) && w > 0 && h > 0 };
            state.canvas.style.left = `${x - screenBounds.left}px`;
            state.canvas.style.top = `${y - screenBounds.top}px`;
            state.canvas.style.width = `${w}px`;
            state.canvas.style.height = `${h}px`;
            const dpr = window.devicePixelRatio || 1;
            state.canvas.width = Math.max(1, Math.round(w * dpr));
            state.canvas.height = Math.max(1, Math.round(h * dpr));
            scheduleStack();
        },
        remove() {
            state.observer?.disconnect();
            state.screen?.removeEventListener("focusin", scheduleStack, true);
            state.canvas?.remove();
            state.canvas = null;
            state.screen = null;
            state.observer = null;
        },
        inspect() {
            if (!state.canvas || !state.screen)
                return { ok: false, reason: "Canvas not attached", transientCount: 0 };
            const windows = Array.from(state.screen.querySelectorAll(".qt-decorated-window"));
            const ranked = windows.map(element => ({
                element,
                canvas: element.querySelector("canvas.qt-window-canvas"),
            }));
            ranked.sort((a, b) => {
                const area = item => {
                    const bounds = item.canvas?.getBoundingClientRect();
                    return bounds ? bounds.width * bounds.height : 0;
                };
                return area(b) - area(a);
            });
            const windowZ = element => Number.parseInt(getComputedStyle(element).zIndex, 10);
            const canvasZ = windowZ(state.canvas);
            const mainWindowZ = ranked.length ? windowZ(ranked[0].element) : null;
            const transientZ = ranked.slice(1).map(item => windowZ(item.element));
            const ok = state.canvas.isConnected
                && state.canvas.parentElement === state.screen
                && canvasZ === 20
                && mainWindowZ !== null && mainWindowZ < canvasZ
                && transientZ.every(z => z > canvasZ);
            return {
                ok,
                canvasZ,
                mainWindowZ,
                transientCount: transientZ.length,
                transientZ,
                backingCanvas: state.canvas.id,
            };
        }
    };
})();
