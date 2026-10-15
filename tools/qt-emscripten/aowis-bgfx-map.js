/* Production experimental bgfx canvas; only enabled with mapWasmRenderer="bgfx".
 * Keep all ordinary Qt widgets BELOW the independent canvas. Only top-level
 * Qt transient dialogs/popups may be above it. Unlike the eventual HUD, this
 * first-step test surface intentionally has pointer-events:none, so camera
 * input continues to be handled by the existing underlying Qt MapWidget.
 */
(function () {
    "use strict";
    const state = { canvas: null, screen: null, observer: null, pending: false, visible: false };
    function screen() {
        return document.getElementById("qt-shadow-container")?.shadowRoot?.querySelector(".qt-screen") || null;
    }
    function stack() {
        state.pending = false;
        if (!state.canvas || !state.screen)
            return;
        const windows = Array.from(state.screen.querySelectorAll(".qt-decorated-window"));
        const ranked = windows.map(element => ({
            element, canvas: element.querySelector("canvas.qt-window-canvas")
        }));
        ranked.sort((a, b) => {
            const area = item => {
                const bounds = item.canvas?.getBoundingClientRect();
                return bounds ? bounds.width * bounds.height : 0;
            };
            return area(b) - area(a);
        });
        if (!ranked.length) {
            state.canvas.style.display = "none";
            return;
        }
        ranked[0].element.style.setProperty("z-index", "10", "important");
        state.canvas.style.setProperty("z-index", "20", "important");
        ranked.slice(1).forEach((item, index) => {
            item.element.style.setProperty("z-index", String(30 + index), "important");
        });
        state.canvas.style.display = state.visible ? "block" : "none";
    }
    function scheduleStack() {
        if (!state.pending) {
            state.pending = true;
            requestAnimationFrame(stack);
        }
    }
    window.aowisBgfxMap = {
        canvas() { return state.canvas; },
        prepare() {
            if (state.canvas) return true;
            const canvas = document.createElement("canvas");
            canvas.id = "aowis-bgfx-map-canvas";
            canvas.style.position = "absolute";
            canvas.style.display = "none";
            canvas.style.background = "#16324a";
            canvas.style.pointerEvents = "none";
            state.canvas = canvas;
            return true;
        },
        attach() {
            if (!state.canvas) return false;
            const host = screen();
            if (!host) return false;
            if (state.screen === host && state.canvas.isConnected) return true;
            state.screen = host;
            host.appendChild(state.canvas);
            state.observer = new MutationObserver(scheduleStack);
            state.observer.observe(host, { childList: true });
            host.addEventListener("focusin", scheduleStack, true);
            scheduleStack();
            return true;
        },
        geometry(x, y, w, h, visible) {
            if (!state.screen || !state.canvas) return;
            const bounds = state.screen.getBoundingClientRect();
            const style = state.canvas.style;
            const left = `${x - bounds.left}px`, top = `${y - bounds.top}px`;
            const width = `${w}px`, height = `${h}px`;
            if (style.left !== left) style.left = left;
            if (style.top !== top) style.top = top;
            if (style.width !== width) style.width = width;
            if (style.height !== height) style.height = height;
            const dpr = window.devicePixelRatio || 1;
            const pw = Math.max(1, Math.round(w * dpr));
            const ph = Math.max(1, Math.round(h * dpr));
            if (state.canvas.width !== pw) state.canvas.width = pw;
            if (state.canvas.height !== ph) state.canvas.height = ph;
            const nextVisible = Boolean(visible) && w > 0 && h > 0;
            if (state.visible !== nextVisible) {
                state.visible = nextVisible;
                scheduleStack();
            }
        },
        remove() {
            state.observer?.disconnect();
            state.screen?.removeEventListener("focusin", scheduleStack, true);
            state.canvas?.remove();
            state.canvas = null;
            state.screen = null;
            state.observer = null;
            state.visible = false;
        }
    };
})();
