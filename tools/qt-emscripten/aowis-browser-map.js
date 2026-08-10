(function () {
    "use strict";

    const TILE_SIZE = 256;
    const TILE_MARGIN = 1;
    const MAX_TILE_RETRY_COUNT = 3;
    const MAX_CONCURRENT_TILE_LOADS = Math.max(2, Math.min(4, Number(navigator.hardwareConcurrency) || 4));
    const TILE_SEAM_OVERLAP_PHYSICAL_PIXELS = 1;
    const EARTH_RADIUS_METERS = 6378137;
    const SCALE_MAXIMUM_WIDTH = 140;
    const MOUSE_PAN_RELEASE_TIMEOUT_MS = 100;
    const MOUSE_PAN_VELOCITY_SMOOTHING = 0.65;
    const MOUSE_PAN_MAXIMUM_SPEED_PIXELS_PER_SECOND = 2400;
    const MOUSE_PAN_MINIMUM_INERTIA_SPEED_PIXELS_PER_SECOND = 70;
    const MOUSE_PAN_INERTIA_DECELERATION_PIXELS_PER_SECOND_SQUARED = 2600;
    const PAN_VELOCITY_STOP_THRESHOLD = 0.5;

    const viewListeners = new Set();
    let activeMapServerConfiguration = null;

    function normalizeMapServerConfiguration(configuration) {
        const baseUrl = typeof configuration?.baseUrl === "string"
            ? configuration.baseUrl.trim().replace(/\/+$/, "")
            : "";
        if (!baseUrl)
            throw new Error("Map server base_url is missing from aowis-server-gui.ini");

        return Object.freeze({
            baseUrl,
            apiKey: typeof configuration?.apiKey === "string" ? configuration.apiKey.trim() : ""
        });
    }

    window.aowisSetMapServerConfiguration = function (configuration) {
        activeMapServerConfiguration = normalizeMapServerConfiguration(configuration);
        console.info("Activated browser map server configuration:", {
            baseUrl: activeMapServerConfiguration.baseUrl,
            hasApiKey: activeMapServerConfiguration.apiKey.length > 0
        });
    };

    window.aowisGetMapServerConfiguration = function () {
        if (!activeMapServerConfiguration)
            return null;

        return {
            baseUrl: activeMapServerConfiguration.baseUrl,
            hasApiKey: activeMapServerConfiguration.apiKey.length > 0
        };
    };

    function mapServerConfiguration() {
        if (!activeMapServerConfiguration)
            throw new Error("Map server configuration has not been activated");

        return activeMapServerConfiguration;
    }

    function tileRequestHeaders() {
        const headers = new Headers({ Accept: "image/*" });
        const configuration = mapServerConfiguration();
        if (configuration.apiKey)
            headers.set("X-API-Key", configuration.apiKey);
        return headers;
    }

    const state = {
        layer: null,
        tilePane: null,
        attribution: null,
        scaleControl: null,
        scaleLabel: null,
        scaleBar: null,
        crosshair: null,
        crosshairImage: "",
        screen: null,
        overlayWindow: null,
        windowObserver: null,
        stackingEventHandler: null,
        tiles: new Map(),
        tileLoadQueue: [],
        tileLoadsActive: 0,
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
        topmost: false,
        mousePan: {
            active: false,
            velocityX: 0,
            velocityY: 0,
            lastMoveTimestamp: 0,
            dragDistance: 0,
            inertiaActive: false,
            inertiaLastTimestamp: 0,
            fractionalDeltaX: 0,
            fractionalDeltaY: 0
        }
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

    function vectorLength(x, y) {
        return Math.hypot(x, y);
    }

    function resetMousePanState() {
        state.mousePan.active = false;
        state.mousePan.velocityX = 0;
        state.mousePan.velocityY = 0;
        state.mousePan.lastMoveTimestamp = 0;
        state.mousePan.dragDistance = 0;
        state.mousePan.inertiaActive = false;
        state.mousePan.inertiaLastTimestamp = 0;
        state.mousePan.fractionalDeltaX = 0;
        state.mousePan.fractionalDeltaY = 0;
    }

    function mousePanBegin(ownerId) {
        if (state.activeOwner !== (ownerId | 0))
            return;

        resetMousePanState();
        state.mousePan.active = true;
    }

    function mousePanMove(ownerId, deltaX, deltaY, elapsedMs) {
        if (state.activeOwner !== (ownerId | 0) || !state.mousePan.active)
            return;

        const x = Number(deltaX) || 0;
        const y = Number(deltaY) || 0;
        const elapsed = Number(elapsedMs) || 0;
        state.mousePan.dragDistance += Math.abs(x) + Math.abs(y);
        state.mousePan.lastMoveTimestamp = performance.now();

        if ((x !== 0 || y !== 0) && elapsed > 0) {
            const elapsedSeconds = Math.max(0.001, Math.min(0.1, elapsed / 1000));
            let measuredVelocityX = x / elapsedSeconds;
            let measuredVelocityY = y / elapsedSeconds;
            const measuredSpeed = vectorLength(measuredVelocityX, measuredVelocityY);

            if (measuredSpeed > MOUSE_PAN_MAXIMUM_SPEED_PIXELS_PER_SECOND) {
                const scale = MOUSE_PAN_MAXIMUM_SPEED_PIXELS_PER_SECOND / measuredSpeed;
                measuredVelocityX *= scale;
                measuredVelocityY *= scale;
            }

            if (state.mousePan.velocityX === 0 && state.mousePan.velocityY === 0) {
                state.mousePan.velocityX = measuredVelocityX;
                state.mousePan.velocityY = measuredVelocityY;
            } else {
                state.mousePan.velocityX =
                    state.mousePan.velocityX * (1 - MOUSE_PAN_VELOCITY_SMOOTHING) +
                    measuredVelocityX * MOUSE_PAN_VELOCITY_SMOOTHING;
                state.mousePan.velocityY =
                    state.mousePan.velocityY * (1 - MOUSE_PAN_VELOCITY_SMOOTHING) +
                    measuredVelocityY * MOUSE_PAN_VELOCITY_SMOOTHING;
            }
        }
    }

    function mousePanRelease(ownerId, startDragDistance) {
        if (state.activeOwner !== (ownerId | 0) || !state.mousePan.active)
            return false;

        state.mousePan.active = false;

        const now = performance.now();
        const movementIsRecent = state.mousePan.lastMoveTimestamp > 0 &&
            now - state.mousePan.lastMoveTimestamp <= MOUSE_PAN_RELEASE_TIMEOUT_MS;
        const releaseSpeed = vectorLength(state.mousePan.velocityX, state.mousePan.velocityY);
        const draggedFarEnough = state.mousePan.dragDistance >= Math.max(0, Number(startDragDistance) || 0);

        if (draggedFarEnough && movementIsRecent && releaseSpeed >= MOUSE_PAN_MINIMUM_INERTIA_SPEED_PIXELS_PER_SECOND) {
            state.mousePan.inertiaActive = true;
            state.mousePan.inertiaLastTimestamp = now;
            state.mousePan.fractionalDeltaX = 0;
            state.mousePan.fractionalDeltaY = 0;
            return true;
        }

        resetMousePanState();
        return false;
    }

    function inertiaActive(ownerId) {
        return state.activeOwner === (ownerId | 0) && state.mousePan.inertiaActive;
    }

    function takeInertiaDelta(ownerId) {
        if (!inertiaActive(ownerId))
            return { x: 0, y: 0, active: false };

        const now = performance.now();
        const elapsedSeconds = Math.max(0, Math.min(
            0.05, (now - state.mousePan.inertiaLastTimestamp) / 1000));
        state.mousePan.inertiaLastTimestamp = now;

        if (elapsedSeconds <= 0)
            return { x: 0, y: 0, active: true };

        const speed = vectorLength(state.mousePan.velocityX, state.mousePan.velocityY);
        if (speed > 0) {
            const nextSpeed = Math.max(
                0,
                speed - MOUSE_PAN_INERTIA_DECELERATION_PIXELS_PER_SECOND_SQUARED * elapsedSeconds);
            const scale = nextSpeed / speed;
            state.mousePan.velocityX *= scale;
            state.mousePan.velocityY *= scale;
        }

        if (vectorLength(state.mousePan.velocityX, state.mousePan.velocityY) < PAN_VELOCITY_STOP_THRESHOLD) {
            state.mousePan.velocityX = 0;
            state.mousePan.velocityY = 0;
            state.mousePan.inertiaActive = false;
        }

        const preciseDeltaX = state.mousePan.velocityX * elapsedSeconds + state.mousePan.fractionalDeltaX;
        const preciseDeltaY = state.mousePan.velocityY * elapsedSeconds + state.mousePan.fractionalDeltaY;
        const deltaX = Math.trunc(preciseDeltaX);
        const deltaY = Math.trunc(preciseDeltaY);
        state.mousePan.fractionalDeltaX = preciseDeltaX - deltaX;
        state.mousePan.fractionalDeltaY = preciseDeltaY - deltaY;

        return { x: deltaX, y: deltaY, active: state.mousePan.inertiaActive };
    }

    function mousePanCancel(ownerId) {
        if (state.activeOwner !== (ownerId | 0))
            return;

        resetMousePanState();
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

    function metersPerPixel(latitude, zoom) {
        const latitudeRadians = clampLatitude(latitude) * Math.PI / 180;
        return Math.cos(latitudeRadians) * 2 * Math.PI * EARTH_RADIUS_METERS
            / worldSize(zoom);
    }

    function roundedScaleDistance(maximumDistanceMeters) {
        if (!Number.isFinite(maximumDistanceMeters) || maximumDistanceMeters <= 0)
            return 0;

        const exponent = Math.floor(Math.log10(maximumDistanceMeters));
        const magnitude = Math.pow(10, exponent);
        const normalized = maximumDistanceMeters / magnitude;
        let factor = 1;
        if (normalized >= 5)
            factor = 5;
        else if (normalized >= 2)
            factor = 2;
        return factor * magnitude;
    }

    function formattedScaleDistance(distanceMeters) {
        if (distanceMeters >= 1000)
            return `${distanceMeters / 1000} km`;
        return `${distanceMeters} m`;
    }

    function updateScaleControl() {
        if (!state.scaleControl || !state.scaleLabel || !state.scaleBar)
            return;

        if (!state.initialized || state.width <= 0) {
            state.scaleControl.style.display = "none";
            return;
        }

        const maximumWidth = Math.max(60, Math.min(
            SCALE_MAXIMUM_WIDTH, Math.floor(state.width * 0.25)));
        const resolution = metersPerPixel(state.latitude, state.zoom);
        const distance = roundedScaleDistance(resolution * maximumWidth);
        if (!(distance > 0) || !(resolution > 0)) {
            state.scaleControl.style.display = "none";
            return;
        }

        const width = snapToPhysicalPixel(distance / resolution);
        state.scaleLabel.textContent = formattedScaleDistance(distance);
        state.scaleBar.style.width = `${width}px`;
        state.scaleControl.style.display = "flex";
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

    function disposeTile(tile) {
        if (!tile)
            return;

        if (tile.aowisAbortController)
            tile.aowisAbortController.abort();
        if (tile.aowisObjectUrl)
            URL.revokeObjectURL(tile.aowisObjectUrl);

        tile.aowisAbortController = null;
        tile.aowisObjectUrl = "";
        tile.aowisQueued = false;
        tile.remove();
    }

    function clearTiles() {
        state.tileLoadQueue.length = 0;
        for (const tile of state.tiles.values())
            disposeTile(tile);
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

        state.scaleControl = document.createElement("div");
        state.scaleControl.style.position = "absolute";
        state.scaleControl.style.left = "10px";
        state.scaleControl.style.bottom = "10px";
        state.scaleControl.style.display = "none";
        state.scaleControl.style.flexDirection = "column";
        state.scaleControl.style.alignItems = "center";
        state.scaleControl.style.gap = "2px";
        state.scaleControl.style.padding = "3px 6px 4px";
        state.scaleControl.style.background = "rgba(255, 255, 255, 0.82)";
        state.scaleControl.style.color = "#111";
        state.scaleControl.style.font = "11px/1.15 sans-serif";
        state.scaleControl.style.fontWeight = "600";
        state.scaleControl.style.whiteSpace = "nowrap";
        state.scaleControl.style.borderRadius = "2px";
        state.scaleControl.style.zIndex = "20";

        state.scaleLabel = document.createElement("div");
        state.scaleControl.appendChild(state.scaleLabel);

        state.scaleBar = document.createElement("div");
        state.scaleBar.style.height = "7px";
        state.scaleBar.style.borderLeft = "2px solid #111";
        state.scaleBar.style.borderRight = "2px solid #111";
        state.scaleBar.style.borderBottom = "2px solid #111";
        state.scaleBar.style.boxSizing = "border-box";
        state.scaleControl.appendChild(state.scaleBar);
        state.layer.appendChild(state.scaleControl);

        state.crosshair = document.createElement("img");
        state.crosshair.alt = "";
        state.crosshair.draggable = false;
        state.crosshair.style.position = "absolute";
        state.crosshair.style.left = "50%";
        state.crosshair.style.top = "50%";
        state.crosshair.style.width = "40px";
        state.crosshair.style.height = "40px";
        state.crosshair.style.maxWidth = "none";
        state.crosshair.style.objectFit = "contain";
        state.crosshair.style.pointerEvents = "none";
        state.crosshair.style.userSelect = "none";
        state.crosshair.style.transform = "translate(-50%, -50%)";
        state.crosshair.style.zIndex = "30";
        if (state.crosshairImage)
            state.crosshair.src = state.crosshairImage;
        state.layer.appendChild(state.crosshair);

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

    function tileUrl(virtualTileX, tileY, retryCount = 0) {
        const wrappedX = wrapTileX(virtualTileX, state.zoom);
        const configuration = mapServerConfiguration();
        const base = `${configuration.baseUrl}/${state.source}/${state.zoom}/${wrappedX}/${tileY}.png`;
        const parameters = [];
        if (state.cacheRevision !== 0)
            parameters.push(`aowis-cache-revision=${state.cacheRevision}`);
        if (retryCount > 0)
            parameters.push(`aowis-retry=${Date.now()}`);
        return parameters.length === 0 ? base : `${base}?${parameters.join("&")}`;
    }

    async function loadTile(image, virtualTileX, tileY, key, retryCount) {
        if (state.tiles.get(key) !== image)
            return;

        if (image.aowisAbortController)
            image.aowisAbortController.abort();

        const controller = new AbortController();
        image.aowisAbortController = controller;

        try {
            const response = await fetch(tileUrl(virtualTileX, tileY, retryCount), {
                headers: tileRequestHeaders(),
                signal: controller.signal
            });
            if (!response.ok)
                throw new Error(`HTTP ${response.status} ${response.statusText}`);

            const blob = await response.blob();
            if (state.tiles.get(key) !== image || controller.signal.aborted)
                return;

            const objectUrl = URL.createObjectURL(blob);
            if (image.aowisObjectUrl)
                URL.revokeObjectURL(image.aowisObjectUrl);
            image.aowisObjectUrl = objectUrl;
            image.src = objectUrl;

            if (typeof image.decode === "function")
                await image.decode();
            if (state.tiles.get(key) !== image || controller.signal.aborted)
                return;
            image.style.visibility = "visible";
        } catch (error) {
            if (controller.signal.aborted || state.tiles.get(key) !== image)
                return;

            if (retryCount >= MAX_TILE_RETRY_COUNT) {
                console.error("AOWIS map tile request failed:", tileUrl(virtualTileX, tileY), error);
                return;
            }

            window.setTimeout(() => {
                enqueueTileLoad(image, virtualTileX, tileY, key, retryCount + 1);
            }, 500 * Math.pow(2, retryCount));
        } finally {
            if (image.aowisAbortController === controller)
                image.aowisAbortController = null;
        }
    }

    function pumpTileLoads() {
        while (state.tileLoadsActive < MAX_CONCURRENT_TILE_LOADS && state.tileLoadQueue.length > 0) {
            const request = state.tileLoadQueue.shift();
            const image = request.image;
            image.aowisQueued = false;
            if (state.tiles.get(request.key) !== image)
                continue;

            state.tileLoadsActive += 1;
            loadTile(image, request.virtualTileX, request.tileY, request.key, request.retryCount)
                .finally(() => {
                    state.tileLoadsActive = Math.max(0, state.tileLoadsActive - 1);
                    pumpTileLoads();
                });
        }
    }

    function enqueueTileLoad(image, virtualTileX, tileY, key, retryCount) {
        if (state.tiles.get(key) !== image || image.aowisQueued)
            return;
        image.aowisQueued = true;
        state.tileLoadQueue.push({
            image: image,
            virtualTileX: virtualTileX,
            tileY: tileY,
            key: key,
            retryCount: retryCount
        });
        pumpTileLoads();
    }

    function createTile(virtualTileX, tileY, key) {
        const image = document.createElement("img");
        image.alt = "";
        image.draggable = false;
        image.decoding = "async";
        image.loading = "eager";
        image.style.position = "absolute";
        image.style.display = "block";
        image.style.visibility = "hidden";
        image.style.width = `${tileRenderSize()}px`;
        image.style.height = `${tileRenderSize()}px`;
        image.style.maxWidth = "none";
        image.style.userSelect = "none";
        image.style.pointerEvents = "none";
        image.aowisAbortController = null;
        image.aowisObjectUrl = "";
        image.aowisQueued = false;

        state.tilePane.appendChild(image);
        state.tiles.set(key, image);
        enqueueTileLoad(image, virtualTileX, tileY, key, 0);
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
            disposeTile(image);
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

            resetMousePanState();
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
        if (ownerChanged)
            resetMousePanState();
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
        updateScaleControl();
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
        updateScaleControl();
        scheduleRender();
        notifyViewChanged();
    }

    function setCrosshairImage(dataUrl) {
        state.crosshairImage = typeof dataUrl === "string" ? dataUrl : "";
        if (!state.crosshair)
            return;

        if (state.crosshairImage)
            state.crosshair.src = state.crosshairImage;
        else
            state.crosshair.removeAttribute("src");
    }

    function invalidateTiles() {
        state.cacheRevision += 1;
        clearTiles();
        scheduleRender();
    }

    function release(ownerId) {
        if (state.activeOwner !== (ownerId | 0))
            return;

        resetMousePanState();
        state.visible = false;
        state.ready = false;
        state.activeOwner = 0;
        state.stackingGeneration += 1;
        if (state.layer)
            state.layer.style.display = "none";
        notifyViewChanged();
    }

    function destroy() {
        resetMousePanState();
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
        state.scaleControl = null;
        state.scaleLabel = null;
        state.scaleBar = null;
        state.crosshair = null;
        state.screen = null;
        state.overlayWindow = null;
        state.ready = false;
        state.visible = false;
        state.initialized = false;
        state.renderPending = false;
        state.tileLoadQueue.length = 0;
        state.activeOwner = 0;
        state.topmost = false;
        state.stackingGeneration += 1;
        notifyViewChanged();
    }

    window.aowisBrowserMap = {
        setGeometry: setGeometry,
        setView: setView,
        mousePanBegin: mousePanBegin,
        mousePanMove: mousePanMove,
        mousePanRelease: mousePanRelease,
        mousePanCancel: mousePanCancel,
        inertiaActive: inertiaActive,
        takeInertiaDelta: takeInertiaDelta,
        setCrosshairImage: setCrosshairImage,
        invalidateTiles: invalidateTiles,
        release: release,
        destroy: destroy,
        getViewState: getViewState,
        subscribeView: subscribeView,
        projection: {
            tileSize: TILE_SIZE,
            normalizeLongitude: normalizeLongitude,
            clampLatitude: clampLatitude,
            worldSize: worldSize,
            longitudeToWorldPixel: longitudeToWorldPixel,
            latitudeToWorldPixel: latitudeToWorldPixel,
            nearestWrappedWorldPixel: nearestWrappedWorldPixel,
            devicePixelRatio: devicePixelRatio,
            snapToPhysicalPixel: snapToPhysicalPixel
        }
    };
})();
