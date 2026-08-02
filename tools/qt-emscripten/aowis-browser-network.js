(function () {
    "use strict";

    const SVG_NAMESPACE = "http://www.w3.org/2000/svg";
    const TILE_SIZE = 256;
    const REFERENCE_ZOOM = 18;

    const state = {
        svg: null,
        worldGroup: null,
        testLine: null,
        testNode: null,
        unsubscribeView: null,
        anchorOwner: 0,
        geometryOriginX: 0,
        geometryOriginY: 0,
        width: 0,
        height: 0,
        snapshot: null
    };

    function createSvgElement(name) {
        return document.createElementNS(SVG_NAMESPACE, name);
    }

    function ensureOverlay(mapLayer) {
        if (!mapLayer)
            return false;

        if (!state.svg) {
            state.svg = createSvgElement("svg");
            state.svg.id = "aowis-browser-network-layer";
            state.svg.setAttribute("aria-hidden", "true");
            state.svg.style.position = "absolute";
            state.svg.style.inset = "0";
            state.svg.style.width = "100%";
            state.svg.style.height = "100%";
            state.svg.style.display = "none";
            state.svg.style.pointerEvents = "none";
            state.svg.style.overflow = "hidden";
            state.svg.style.zIndex = "10";

            state.worldGroup = createSvgElement("g");
            state.svg.appendChild(state.worldGroup);

            state.testLine = createSvgElement("line");
            state.testLine.setAttribute("stroke", "#ff00ff");
            state.testLine.setAttribute("stroke-width", "5");
            state.testLine.setAttribute("stroke-linecap", "round");
            state.testLine.setAttribute("vector-effect", "non-scaling-stroke");
            state.worldGroup.appendChild(state.testLine);

            state.testNode = createSvgElement("circle");
            state.testNode.setAttribute("fill", "#00ffff");
            state.testNode.setAttribute("stroke", "#111111");
            state.testNode.setAttribute("stroke-width", "3");
            state.testNode.setAttribute("vector-effect", "non-scaling-stroke");
            state.worldGroup.appendChild(state.testNode);
        }

        if (state.svg.parentElement !== mapLayer)
            mapLayer.appendChild(state.svg);

        return true;
    }

    function scaleForZoom(zoom) {
        return Math.pow(2, zoom - REFERENCE_ZOOM);
    }

    function initializeTestGeometry(mapView) {
        const scale = scaleForZoom(mapView.zoom);
        state.anchorOwner = mapView.activeOwner;
        state.geometryOriginX = mapView.centerPixelX / scale;
        state.geometryOriginY = mapView.centerPixelY / scale;

        const halfLineWidth = Math.max(120, mapView.width * 0.25) / scale;
        const halfLineHeight = Math.max(70, mapView.height * 0.15) / scale;
        const nodeRadius = 10 / scale;

        state.testLine.setAttribute("x1", String(-halfLineWidth));
        state.testLine.setAttribute("y1", String(halfLineHeight));
        state.testLine.setAttribute("x2", String(halfLineWidth));
        state.testLine.setAttribute("y2", String(-halfLineHeight));
        state.testNode.setAttribute("cx", "0");
        state.testNode.setAttribute("cy", "0");
        state.testNode.setAttribute("r", String(nodeRadius));
    }

    function updateViewport(width, height) {
        if (!state.svg || width <= 0 || height <= 0)
            return;

        if (state.width === width && state.height === height)
            return;

        state.width = width;
        state.height = height;
        state.svg.setAttribute("viewBox", `0 0 ${width} ${height}`);
    }

    function updateWorldTransform(mapView) {
        if (!state.worldGroup)
            return;

        const scale = scaleForZoom(mapView.zoom);
        const tileOriginPixelX = mapView.originTileX * TILE_SIZE;
        const tileOriginPixelY = mapView.originTileY * TILE_SIZE;
        const translateX = state.geometryOriginX * scale - tileOriginPixelX + mapView.translateX;
        const translateY = state.geometryOriginY * scale - tileOriginPixelY + mapView.translateY;
        state.worldGroup.setAttribute(
            "transform", `translate(${translateX} ${translateY}) scale(${scale})`);
    }

    function handleMapViewChanged(mapView) {
        if (!mapView || !ensureOverlay(mapView.layer)) {
            if (state.svg)
                state.svg.style.display = "none";
            return;
        }

        updateViewport(mapView.width, mapView.height);

        const shouldShow = mapView.topmost && mapView.visible && mapView.ready
            && mapView.initialized && mapView.width > 0 && mapView.height > 0;
        if (!shouldShow) {
            state.svg.style.display = "none";
            return;
        }

        if (state.anchorOwner !== mapView.activeOwner)
            initializeTestGeometry(mapView);

        updateWorldTransform(mapView);
        state.svg.style.display = "block";
    }

    function initialize() {
        if (!window.aowisBrowserMap || typeof window.aowisBrowserMap.subscribeView !== "function")
            throw new Error("AOWIS browser network requires aowis-browser-map.js");

        state.unsubscribeView = window.aowisBrowserMap.subscribeView(handleMapViewChanged);
    }

    // nodes: [renderId, entityType, uuid, hydraulicId, longitude, latitude]
    // links: [renderId, entityType, uuid, hydraulicId, startNodeId, endNodeId, vertices]
    function setSnapshot(snapshot) {
        if (!snapshot || !Array.isArray(snapshot.nodes) || !Array.isArray(snapshot.links))
            throw new TypeError("Invalid AOWIS browser network snapshot");

        state.snapshot = snapshot;
    }

    function destroy() {
        if (state.unsubscribeView)
            state.unsubscribeView();
        state.unsubscribeView = null;

        if (state.svg)
            state.svg.remove();
        state.svg = null;
        state.worldGroup = null;
        state.testLine = null;
        state.testNode = null;
        state.anchorOwner = 0;
        state.geometryOriginX = 0;
        state.geometryOriginY = 0;
        state.width = 0;
        state.height = 0;
        state.snapshot = null;
    }

    window.aowisBrowserNetwork = {
        setSnapshot: setSnapshot,
        destroy: destroy
    };

    initialize();
})();
