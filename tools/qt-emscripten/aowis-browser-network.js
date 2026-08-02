(function () {
    "use strict";

    const SVG_NAMESPACE = "http://www.w3.org/2000/svg";
    const TILE_SIZE = 256;
    const REFERENCE_ZOOM = 18;
    const NETWORK_COLOR = "#b000ff";

    const state = {
        svg: null,
        worldGroup: null,
        linksPath: null,
        nodesPath: null,
        unsubscribeView: null,
        geometryOriginX: 0,
        geometryOriginY: 0,
        linkPathData: "",
        nodePathData: "",
        geometryReady: false,
        width: 0,
        height: 0,
        snapshot: null,
        lastMapView: null
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

            state.linksPath = createSvgElement("path");
            state.linksPath.setAttribute("fill", "none");
            state.linksPath.setAttribute("stroke", NETWORK_COLOR);
            state.linksPath.setAttribute("stroke-width", "3");
            state.linksPath.setAttribute("stroke-linecap", "round");
            state.linksPath.setAttribute("stroke-linejoin", "round");
            state.linksPath.setAttribute("vector-effect", "non-scaling-stroke");
            state.worldGroup.appendChild(state.linksPath);

            state.nodesPath = createSvgElement("path");
            state.nodesPath.setAttribute("fill", "none");
            state.nodesPath.setAttribute("stroke", NETWORK_COLOR);
            state.nodesPath.setAttribute("stroke-width", "8");
            state.nodesPath.setAttribute("stroke-linecap", "round");
            state.nodesPath.setAttribute("vector-effect", "non-scaling-stroke");
            state.worldGroup.appendChild(state.nodesPath);

            applyGeometryToElements();
        }

        if (state.svg.parentElement !== mapLayer)
            mapLayer.appendChild(state.svg);

        return true;
    }

    function scaleForZoom(zoom) {
        return Math.pow(2, zoom - REFERENCE_ZOOM);
    }

    function worldSize(zoom) {
        return TILE_SIZE * Math.pow(2, zoom);
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

    function longitudeToWorldPixel(longitude) {
        return (normalizeLongitude(longitude) + 180) / 360 * worldSize(REFERENCE_ZOOM);
    }

    function latitudeToWorldPixel(latitude) {
        const radians = clampLatitude(latitude) * Math.PI / 180;
        const mercator = Math.log(Math.tan(Math.PI / 4 + radians / 2));
        return (1 - mercator / Math.PI) / 2 * worldSize(REFERENCE_ZOOM);
    }

    function nearestWrappedWorldPixel(rawPixelX, referencePixelX) {
        const size = worldSize(REFERENCE_ZOOM);
        return rawPixelX + Math.round((referencePixelX - rawPixelX) / size) * size;
    }

    function formatted(value) {
        return String(Math.round(value * 1000) / 1000);
    }

    function validCoordinate(coordinate) {
        return Array.isArray(coordinate) && coordinate.length >= 2
            && Number.isFinite(Number(coordinate[0]))
            && Number.isFinite(Number(coordinate[1]));
    }

    function applyGeometryToElements() {
        if (state.linksPath)
            state.linksPath.setAttribute("d", state.linkPathData);
        if (state.nodesPath)
            state.nodesPath.setAttribute("d", state.nodePathData);
    }

    function buildGeometry(snapshot) {
        const projectedNodes = [];
        const nodesByRenderId = new Map();
        const projectedLinks = [];
        let anchorX = null;
        let minimumX = Number.POSITIVE_INFINITY;
        let minimumY = Number.POSITIVE_INFINITY;
        let maximumX = Number.NEGATIVE_INFINITY;
        let maximumY = Number.NEGATIVE_INFINITY;

        function includePoint(x, y) {
            minimumX = Math.min(minimumX, x);
            minimumY = Math.min(minimumY, y);
            maximumX = Math.max(maximumX, x);
            maximumY = Math.max(maximumY, y);
        }

        for (const node of snapshot.nodes) {
            if (!Array.isArray(node) || node.length < 6)
                continue;

            const longitude = Number(node[4]);
            const latitude = Number(node[5]);
            if (!Number.isFinite(longitude) || !Number.isFinite(latitude))
                continue;

            const rawX = longitudeToWorldPixel(longitude);
            if (anchorX === null)
                anchorX = rawX;

            const x = nearestWrappedWorldPixel(rawX, anchorX);
            const y = latitudeToWorldPixel(latitude);
            const projectedNode = { renderId: Number(node[0]), x: x, y: y };
            projectedNodes.push(projectedNode);
            nodesByRenderId.set(projectedNode.renderId, projectedNode);
            includePoint(x, y);
        }

        for (const link of snapshot.links) {
            if (!Array.isArray(link) || link.length < 7 || !Array.isArray(link[6]))
                continue;

            const vertices = [];
            const startNode = nodesByRenderId.get(Number(link[4]));
            let previousX = startNode ? startNode.x : anchorX;

            for (const coordinate of link[6]) {
                if (!validCoordinate(coordinate))
                    continue;

                const rawX = longitudeToWorldPixel(Number(coordinate[0]));
                if (previousX === null)
                    previousX = rawX;

                const x = nearestWrappedWorldPixel(rawX, previousX);
                const y = latitudeToWorldPixel(Number(coordinate[1]));
                vertices.push({ x: x, y: y });
                includePoint(x, y);
                previousX = x;
            }

            if (vertices.length >= 2)
                projectedLinks.push(vertices);
        }

        if (!Number.isFinite(minimumX) || !Number.isFinite(minimumY)) {
            state.geometryOriginX = 0;
            state.geometryOriginY = 0;
            state.linkPathData = "";
            state.nodePathData = "";
            state.geometryReady = false;
            applyGeometryToElements();
            return;
        }

        state.geometryOriginX = (minimumX + maximumX) / 2;
        state.geometryOriginY = (minimumY + maximumY) / 2;

        const linkCommands = [];
        for (const vertices of projectedLinks) {
            linkCommands.push(
                "M", formatted(vertices[0].x - state.geometryOriginX), " ",
                formatted(vertices[0].y - state.geometryOriginY));
            for (let index = 1; index < vertices.length; ++index) {
                linkCommands.push(
                    "L", formatted(vertices[index].x - state.geometryOriginX), " ",
                    formatted(vertices[index].y - state.geometryOriginY));
            }
        }

        const nodeCommands = [];
        for (const node of projectedNodes) {
            nodeCommands.push(
                "M", formatted(node.x - state.geometryOriginX), " ",
                formatted(node.y - state.geometryOriginY), "h0.001");
        }

        state.linkPathData = linkCommands.join("");
        state.nodePathData = nodeCommands.join("");
        state.geometryReady = projectedNodes.length > 0 || projectedLinks.length > 0;
        applyGeometryToElements();
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
        if (!state.worldGroup || !state.geometryReady)
            return;

        const scale = scaleForZoom(mapView.zoom);
        const size = worldSize(mapView.zoom);
        let geometryOriginPixelX = state.geometryOriginX * scale;
        geometryOriginPixelX += Math.round(
            (mapView.centerPixelX - geometryOriginPixelX) / size) * size;

        const geometryOriginPixelY = state.geometryOriginY * scale;
        const tileOriginPixelX = mapView.originTileX * TILE_SIZE;
        const tileOriginPixelY = mapView.originTileY * TILE_SIZE;
        const translateX = geometryOriginPixelX - tileOriginPixelX + mapView.translateX;
        const translateY = geometryOriginPixelY - tileOriginPixelY + mapView.translateY;
        state.worldGroup.setAttribute(
            "transform", `translate(${translateX} ${translateY}) scale(${scale})`);
    }

    function handleMapViewChanged(mapView) {
        state.lastMapView = mapView;

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
        buildGeometry(snapshot);
        if (state.lastMapView)
            handleMapViewChanged(state.lastMapView);
    }

    function destroy() {
        if (state.unsubscribeView)
            state.unsubscribeView();
        state.unsubscribeView = null;

        if (state.svg)
            state.svg.remove();
        state.svg = null;
        state.worldGroup = null;
        state.linksPath = null;
        state.nodesPath = null;
        state.geometryOriginX = 0;
        state.geometryOriginY = 0;
        state.linkPathData = "";
        state.nodePathData = "";
        state.geometryReady = false;
        state.width = 0;
        state.height = 0;
        state.snapshot = null;
        state.lastMapView = null;
    }

    window.aowisBrowserNetwork = {
        setSnapshot: setSnapshot,
        destroy: destroy
    };

    initialize();
})();
