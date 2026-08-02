(function () {
    "use strict";

    const TILE_SIZE = 256;
    const TILE_MARGIN = 1;
    const MAP_SERVER_BASE_URL = "http://aowis-server-map.localhost:80";
    const MAX_TILE_RETRY_COUNT = 3;
    const TILE_SEAM_OVERLAP_PHYSICAL_PIXELS = 1;

    const viewListeners = new Set();

    const state = {
        layer: null,
        tilePane: null,
        attribution: null,
        screen: null,
        overlayWindow: null,
        windowObserver: null,
        stackingEventHandler: null,
        tiles: new Map(),
        x: 0,
        y: 0,
        width: 0,
        height: 0,
        visible: false,
        ready: false,
        initialized: false,
        longitude: 0,
        latitude: 0,
        zoom: 18,
        provider: 1,
        source: "arcgis",
        centerPixelX: 0,
        centerPixelY: 0,
        originTileX: 0,
        originTileY: 0,
        translateX: 0,
        translateY: 0,
        cacheRevision: 0,
        renderPending: false,
        stackingGeneration: 0,
        activeOwner: 0,
        topmost: false
    };

    function getViewState() {
        return {
            layer: state.layer,
            tilePane: state.tilePane,
            activeOwner: state.activeOwner,
            topmost: state.topmost,
            visible: state.visible,
            ready: state.ready,
            initialized: state.initialized,
            width: state.width,
            height: state.height,
            longitude: state.longitude,
            latitude: state.latitude,
            zoom: state.zoom,
            centerPixelX: state.centerPixelX,
            centerPixelY: state.centerPixelY,
            originTileX: state.originTileX,
            originTileY: state.originTileY,
            translateX: state.translateX,
            translateY: state.translateY
        };
    }

    function notifyViewChanged() {
        const viewState = getViewState();
        for (const listener of viewListeners) {
            try {
                listener(viewState);
            } catch (error) {
                console.error("AOWIS browser map view listener failed:", error);
            }
        }
    }

    function subscribeView(listener) {
        if (typeof listener !== "function")
            throw new TypeError("AOWIS browser map view listener must be a function");

        viewListeners.add(listener);
        listener(getViewState());
        return () => viewListeners.delete(listener);
    }

    function normalizeLongitude(longitude) {
        let normalized = (longitude + 180) % 360;
        if (normalized < 0)
            normalized += 360;
        return normalized - 180;
    }

    function clampLatitude(latitude) {
        return Math.max(-85.0511287798066, Math.min(85.0511287798066, latitude));
    }

    function sourceFor(provider, zoom) {
        if (zoom > 17)
            return "osmcyclo";

        switch (provider) {
        case 1:
            return "arcgis";
        case 2:
            return "openstreetmap";
        case 3:
            return "opentopomap";
        case 4:
            return "osmcyclo";
        default:
            return "arcgis";
        }
    }

    function attributionFor(source) {
        switch (source) {
        case "arcgis":
            return "Tiles © Esri";
        case "opentopomap":
            return "Map data © OpenStreetMap contributors, SRTM | Style © OpenTopoMap";
        case "osmcyclo":
            return "Map data © OpenStreetMap contributors | Style © CyclOSM";
        case "openstreetmap":
        default:
            return "© OpenStreetMap contributors";
        }
    }

    function devicePixelRatio() {
        return Math.max(1, window.devicePixelRatio || 1);
    }

    function snapToPhysicalPixel(value) {
        const ratio = devicePixelRatio();
        return Math.round(value * ratio) / ratio;
    }

    function tileRenderSize() {
        return TILE_SIZE + TILE_SEAM_OVERLAP_PHYSICAL_PIXELS / devicePixelRatio();
    }

    function worldSize(zoom) {
        return TILE_SIZE * Math.pow(2, zoom);
    }

    function longitudeToWorldPixel(longitude, zoom) {
        const normalized = normalizeLongitude(longitude);
        return (normalized + 180) / 360 * worldSize(zoom);
    }

    function latitudeToWorldPixel(latitude, zoom) {
        const bounded = clampLatitude(latitude);
        const radians = bounded * Math.PI / 180;
        const mercator = Math.log(Math.tan(Math.PI / 4 + radians / 2));
        return (1 - mercator / Math.PI) / 2 * worldSize(zoom);
    }

    function nearestWrappedWorldPixel(rawPixelX, previousPixelX, zoom) {
        const size = worldSize(zoom);
        return rawPixelX + Math.round((previousPixelX - rawPixelX) / size) * size;
    }

    function wrapTileX(tileX, zoom) {
        const count = Math.pow(2, zoom);
        return ((tileX % count) + count) % count;
    }

    function clearTiles() {
        for (const tile of state.tiles.values())
            tile.remove();
        state.tiles.clear();
    }

    function getShadowScreen() {
        const shadowHost = document.getElementById("qt-shadow-container");
        const shadowRoot = shadowHost ? shadowHost.shadowRoot : null;
        return shadowRoot ? shadowRoot.querySelector(".qt-screen") : null;
    }

    function decoratedWindowsExcept(mainWindow, excludedWindow) {
        const windows = Array.from(state.screen.querySelectorAll(".qt-decorated-window"));
        return windows.filter((windowElement) =>
            windowElement !== mainWindow && windowElement !== excludedWindow);
    }

    function raiseTransientWindows(mainWindow, excludedWindow, firstZIndex) {
        const windows = decoratedWindowsExcept(mainWindow, excludedWindow);
        windows.sort((first, second) => {
            const firstZIndex = Number.parseInt(window.getComputedStyle(first).zIndex, 10) || 0;
            const secondZIndex = Number.parseInt(window.getComputedStyle(second).zIndex, 10) || 0;
            if (firstZIndex !== secondZIndex)
                return firstZIndex - secondZIndex;
            const position = first.compareDocumentPosition(second);
            return position & Node.DOCUMENT_POSITION_FOLLOWING ? -1 : 1;
        });

        windows.forEach((windowElement, index) => {
            windowElement.style.setProperty(
                "z-index", String(firstZIndex + index), "important");
        });
    }

    function requestStackingRefresh() {
        if (!state.visible)
            return;

        const generation = ++state.stackingGeneration;
        window.requestAnimationFrame(() => {
            synchronizeStacking(
                state.x, state.y, state.width, state.height, 30, generation);
        });
    }

    function ensureLayer() {
        const screen = getShadowScreen();
        if (!screen)
            return false;

        if (state.layer && state.screen === screen)
            return true;

        if (state.layer)
            state.layer.remove();
        if (state.stackingEventHandler && state.screen)
            state.screen.removeEventListener("focusin", state.stackingEventHandler, true);

        state.screen = screen;
        state.layer = document.createElement("div");
        state.layer.id = "aowis-browser-map-layer";
        state.layer.setAttribute("aria-hidden", "true");
        state.layer.style.position = "absolute";
        state.layer.style.display = "none";
        state.layer.style.overflow = "hidden";
        state.layer.style.pointerEvents = "none";
        state.layer.style.background = "#d4d4d4";
        state.layer.style.zIndex = "20";
        state.layer.style.contain = "strict";

        state.tilePane = document.createElement("div");
        state.tilePane.style.position = "absolute";
        state.tilePane.style.left = "0";
        state.tilePane.style.top = "0";
        state.tilePane.style.width = "0";
        state.tilePane.style.height = "0";
        state.tilePane.style.willChange = "transform";
        state.tilePane.style.transformOrigin = "0 0";
        state.tilePane.style.zIndex = "0";
        state.layer.appendChild(state.tilePane);

        state.attribution = document.createElement("div");
        state.attribution.style.position = "absolute";
        state.attribution.style.right = "0";
        state.attribution.style.bottom = "0";
        state.attribution.style.padding = "2px 5px";
        state.attribution.style.background = "rgba(255, 255, 255, 0.78)";
        state.attribution.style.color = "#222";
        state.attribution.style.font = "10px/1.25 sans-serif";
        state.attribution.style.whiteSpace = "nowrap";
        state.attribution.style.zIndex = "20";
        state.layer.appendChild(state.attribution);

        screen.appendChild(state.layer);

        if (state.windowObserver)
            state.windowObserver.disconnect();
        state.windowObserver = new MutationObserver(requestStackingRefresh);
        state.windowObserver.observe(screen, { childList: true });

        state.stackingEventHandler = requestStackingRefresh;
        screen.addEventListener("focusin", state.stackingEventHandler, true);

        clearTiles();
        state.ready = false;
        notifyViewChanged();
        return true;
    }

    function tileUrl(virtualTileX, tileY) {
        const wrappedX = wrapTileX(virtualTileX, state.zoom);
        const base = `${MAP_SERVER_BASE_URL}/${state.source}/${state.zoom}/${wrappedX}/${tileY}.png`;
        if (state.cacheRevision === 0)
            return base;
        return `${base}?aowis-cache-revision=${state.cacheRevision}`;
    }

    function createTile(virtualTileX, tileY, key) {
        const image = document.createElement("img");
        image.alt = "";
        image.draggable = false;
        image.decoding = "async";
        image.loading = "eager";
        image.style.position = "absolute";
        image.style.display = "block";
        image.style.width = `${tileRenderSize()}px`;
        image.style.height = `${tileRenderSize()}px`;
        image.style.maxWidth = "none";
        image.style.userSelect = "none";
        image.style.pointerEvents = "none";
        image.dataset.retryCount = "0";

        const load = () => {
            image.src = tileUrl(virtualTileX, tileY);
        };

        image.addEventListener("error", () => {
            if (state.tiles.get(key) !== image)
                return;

            const retryCount = Number(image.dataset.retryCount || 0);
            if (retryCount >= MAX_TILE_RETRY_COUNT)
                return;

            image.dataset.retryCount = String(retryCount + 1);
            window.setTimeout(() => {
                if (state.tiles.get(key) !== image)
                    return;
                const separator = tileUrl(virtualTileX, tileY).includes("?") ? "&" : "?";
                image.src = `${tileUrl(virtualTileX, tileY)}${separator}aowis-retry=${Date.now()}`;
            }, 500 * Math.pow(2, retryCount));
        });

        load();
        state.tilePane.appendChild(image);
        state.tiles.set(key, image);
        return image;
    }

    function applyTransform() {
        if (!state.tilePane || !state.initialized)
            return;

        const originPixelX = state.originTileX * TILE_SIZE;
        const originPixelY = state.originTileY * TILE_SIZE;
        state.translateX = snapToPhysicalPixel(
            state.width / 2 - (state.centerPixelX - originPixelX));
        state.translateY = snapToPhysicalPixel(
            state.height / 2 - (state.centerPixelY - originPixelY));
        state.tilePane.style.transform = `translate3d(${state.translateX}px, ${state.translateY}px, 0)`;
    }

    function renderTiles() {
        state.renderPending = false;
        if (!state.visible || !state.ready || !state.initialized || !state.layer)
            return;

        const tileCount = Math.pow(2, state.zoom);
        const minimumVirtualX = Math.floor((state.centerPixelX - state.width / 2) / TILE_SIZE) - TILE_MARGIN;
        const maximumVirtualX = Math.floor((state.centerPixelX + state.width / 2) / TILE_SIZE) + TILE_MARGIN;
        const minimumTileY = Math.max(0, Math.floor((state.centerPixelY - state.height / 2) / TILE_SIZE) - TILE_MARGIN);
        const maximumTileY = Math.min(tileCount - 1, Math.floor((state.centerPixelY + state.height / 2) / TILE_SIZE) + TILE_MARGIN);
        const nextOriginTileX = Math.floor(state.centerPixelX / TILE_SIZE);
        const nextOriginTileY = Math.floor(state.centerPixelY / TILE_SIZE);
        const originChanged = nextOriginTileX !== state.originTileX || nextOriginTileY !== state.originTileY;

        state.originTileX = nextOriginTileX;
        state.originTileY = nextOriginTileY;

        const required = new Set();
        for (let virtualTileX = minimumVirtualX; virtualTileX <= maximumVirtualX; ++virtualTileX) {
            for (let tileY = minimumTileY; tileY <= maximumTileY; ++tileY) {
                const key = `${state.source}:${state.zoom}:${state.cacheRevision}:${virtualTileX}:${tileY}`;
                required.add(key);

                let image = state.tiles.get(key);
                if (!image)
                    image = createTile(virtualTileX, tileY, key);

                if (originChanged || !image.dataset.positioned) {
                    image.style.left = `${(virtualTileX - state.originTileX) * TILE_SIZE}px`;
                    image.style.top = `${(tileY - state.originTileY) * TILE_SIZE}px`;
                    image.dataset.positioned = "1";
                }
            }
        }

        for (const [key, image] of state.tiles) {
            if (required.has(key))
                continue;
            image.remove();
            state.tiles.delete(key);
        }

        applyTransform();
        if (originChanged)
            notifyViewChanged();
    }

    function scheduleRender() {
        if (state.renderPending)
            return;

        state.renderPending = true;
        window.requestAnimationFrame(renderTiles);
    }

    function synchronizeStacking(x, y, width, height, remainingAttempts, generation) {
        if (state.stackingGeneration !== generation || !ensureLayer())
            return;

        const screen = state.screen;
        const canvases = Array.from(screen.querySelectorAll("canvas.qt-window-canvas"));
        let mainCanvas = null;
        let largestArea = -1;

        for (const canvas of canvases) {
            const rect = canvas.getBoundingClientRect();
            const area = rect.width * rect.height;
            if (area > largestArea) {
                largestArea = area;
                mainCanvas = canvas;
            }
        }

        const mainWindow = mainCanvas ? mainCanvas.closest(".qt-decorated-window") : null;

        if (state.topmost) {
            if (!mainWindow) {
                state.ready = false;
                state.layer.style.display = "none";
                if (remainingAttempts > 0) {
                    window.requestAnimationFrame(() => {
                        synchronizeStacking(x, y, width, height, remainingAttempts - 1, generation);
                    });
                }
                return;
            }

            mainWindow.style.setProperty("z-index", "10", "important");
            state.layer.style.setProperty("z-index", "20", "important");
            raiseTransientWindows(mainWindow, null, 30);
            state.overlayWindow = null;
            state.ready = true;
            state.layer.style.display = state.visible ? "block" : "none";
            scheduleRender();
            notifyViewChanged();
            return;
        }

        let overlayWindow = null;
        let overlayCanvas = null;
        let bestScore = Number.POSITIVE_INFINITY;

        for (const canvas of canvases) {
            const candidateWindow = canvas.closest(".qt-decorated-window");
            if (!candidateWindow || candidateWindow === mainWindow)
                continue;

            const rect = canvas.getBoundingClientRect();
            const score = Math.abs(rect.left - x) + Math.abs(rect.top - y)
                + Math.abs(rect.width - width) + Math.abs(rect.height - height);
            if (score < bestScore) {
                bestScore = score;
                overlayWindow = candidateWindow;
                overlayCanvas = canvas;
            }
        }

        const overlayReady = overlayWindow && bestScore <= 12;
        if (mainWindow)
            mainWindow.style.setProperty("z-index", "10", "important");

        state.layer.style.setProperty("z-index", "20", "important");

        if (overlayReady) {
            overlayWindow.style.setProperty("z-index", "30", "important");
            overlayWindow.style.setProperty("background-color", "transparent", "important");
            overlayWindow.style.setProperty("box-shadow", "none", "important");
            overlayWindow.style.setProperty("border", "0", "important");

            const qtWindow = overlayWindow.querySelector(".qt-window");
            if (qtWindow)
                qtWindow.style.setProperty("background-color", "transparent", "important");

            overlayCanvas.style.setProperty("background-color", "transparent", "important");
            overlayCanvas.style.setProperty("opacity", "1", "important");

            raiseTransientWindows(mainWindow, overlayWindow, 40);

            state.overlayWindow = overlayWindow;
            state.ready = true;
            state.layer.style.display = state.visible ? "block" : "none";
            scheduleRender();
            notifyViewChanged();
            return;
        }

        state.ready = false;
        state.layer.style.display = "none";
        notifyViewChanged();
        if (remainingAttempts > 0) {
            window.requestAnimationFrame(() => {
                synchronizeStacking(x, y, width, height, remainingAttempts - 1, generation);
            });
        }
    }

    function setGeometry(ownerId, x, y, width, height, visible, topmost) {
        const owner = ownerId | 0;
        const nextVisible = Boolean(visible) && width > 0 && height > 0;

        if (!nextVisible) {
            if (state.activeOwner !== owner)
                return;

            state.visible = false;
            state.ready = false;
            state.activeOwner = 0;
            state.stackingGeneration += 1;
            if (state.layer)
                state.layer.style.display = "none";
            notifyViewChanged();
            return;
        }

        const ownerChanged = state.activeOwner !== owner;
        const presentationChanged = state.topmost !== Boolean(topmost);
        state.activeOwner = owner;
        state.topmost = Boolean(topmost);
        state.x = x;
        state.y = y;
        state.width = Math.max(0, width);
        state.height = Math.max(0, height);
        state.visible = true;

        if (ownerChanged) {
            state.initialized = false;
            clearTiles();
        }
        if (ownerChanged || presentationChanged) {
            state.ready = false;
            state.overlayWindow = null;
            if (state.layer)
                state.layer.style.display = "none";
        }

        if (!ensureLayer()) {
            const generation = ++state.stackingGeneration;
            window.requestAnimationFrame(() => {
                synchronizeStacking(x, y, width, height, 120, generation);
            });
            return;
        }

        const screenRect = state.screen.getBoundingClientRect();
        state.layer.style.left = `${x - screenRect.left}px`;
        state.layer.style.top = `${y - screenRect.top}px`;
        state.layer.style.width = `${state.width}px`;
        state.layer.style.height = `${state.height}px`;
        state.layer.style.display = state.ready ? "block" : "none";

        applyTransform();
        scheduleRender();
        notifyViewChanged();

        const generation = ++state.stackingGeneration;
        window.requestAnimationFrame(() => {
            synchronizeStacking(x, y, width, height, 120, generation);
        });
    }

    function setView(ownerId, longitude, latitude, zoom, provider) {
        if (state.activeOwner !== (ownerId | 0))
            return;

        const boundedZoom = Math.max(1, Math.min(19, zoom | 0));
        const nextSource = sourceFor(provider | 0, boundedZoom);
        const zoomChanged = !state.initialized || boundedZoom !== state.zoom;
        const sourceChanged = !state.initialized || nextSource !== state.source;
        const rawPixelX = longitudeToWorldPixel(longitude, boundedZoom);
        const nextPixelX = zoomChanged
            ? rawPixelX
            : nearestWrappedWorldPixel(rawPixelX, state.centerPixelX, boundedZoom);

        state.longitude = normalizeLongitude(longitude);
        state.latitude = clampLatitude(latitude);
        state.zoom = boundedZoom;
        state.provider = provider | 0;
        state.source = nextSource;
        state.centerPixelX = nextPixelX;
        state.centerPixelY = latitudeToWorldPixel(state.latitude, boundedZoom);
        state.initialized = true;

        if (zoomChanged || sourceChanged) {
            clearTiles();
            state.originTileX = Math.floor(state.centerPixelX / TILE_SIZE);
            state.originTileY = Math.floor(state.centerPixelY / TILE_SIZE);
        }

        if (state.attribution)
            state.attribution.textContent = attributionFor(state.source);

        applyTransform();
        scheduleRender();
        notifyViewChanged();
    }

    function invalidateTiles() {
        state.cacheRevision += 1;
        clearTiles();
        scheduleRender();
    }

    function release(ownerId) {
        if (state.activeOwner !== (ownerId | 0))
            return;

        state.visible = false;
        state.ready = false;
        state.activeOwner = 0;
        state.stackingGeneration += 1;
        if (state.layer)
            state.layer.style.display = "none";
        notifyViewChanged();
    }

    function destroy() {
        clearTiles();
        if (state.layer)
            state.layer.remove();

        if (state.windowObserver)
            state.windowObserver.disconnect();
        state.windowObserver = null;
        if (state.stackingEventHandler && state.screen)
            state.screen.removeEventListener("focusin", state.stackingEventHandler, true);
        state.stackingEventHandler = null;
        state.layer = null;
        state.tilePane = null;
        state.attribution = null;
        state.screen = null;
        state.overlayWindow = null;
        state.ready = false;
        state.visible = false;
        state.initialized = false;
        state.renderPending = false;
        state.activeOwner = 0;
        state.topmost = false;
        state.stackingGeneration += 1;
        notifyViewChanged();
    }

    window.aowisBrowserMap = {
        setGeometry: setGeometry,
        setView: setView,
        invalidateTiles: invalidateTiles,
        release: release,
        destroy: destroy,
        getViewState: getViewState,
        subscribeView: subscribeView
    };
})();
