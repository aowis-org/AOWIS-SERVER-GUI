(function () {
    "use strict";

    const SVG_NAMESPACE = "http://www.w3.org/2000/svg";
    const TILE_SIZE = 256;
    const REFERENCE_ZOOM = 18;
    const NETWORK_COLOR = "#b000ff";
    const LINK_HIT_DISTANCE = 7;
    const SPATIAL_CELL_SIZE = 128;
    const ENTITY_PIPE = 4;
    const ENTITY_PUMP = 5;
    const ENTITY_VALVE = 6;

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
        lastMapView: null,
        markers: [],
        deviceSegments: [],
        pipeSegments: [],
        globalDeviceSegments: [],
        globalPipeSegments: [],
        spatialCells: new Map()
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

    function markerWidthForZoom(zoom) {
        if (zoom === 19)
            return 40;
        if (zoom === 18)
            return 30;
        if (zoom === 17)
            return 20;
        return 10;
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

    function spatialCellCoordinate(value) {
        return Math.floor(value / SPATIAL_CELL_SIZE);
    }

    function spatialCellKey(x, y) {
        return `${x}:${y}`;
    }

    function spatialCell(x, y) {
        const key = spatialCellKey(x, y);
        let cell = state.spatialCells.get(key);
        if (!cell) {
            cell = { markers: [], deviceSegments: [], pipeSegments: [] };
            state.spatialCells.set(key, cell);
        }
        return cell;
    }

    function addMarkerToSpatialIndex(markerIndex) {
        const marker = state.markers[markerIndex];
        spatialCell(spatialCellCoordinate(marker.x), spatialCellCoordinate(marker.y))
            .markers.push(markerIndex);
    }

    function addSegmentToSpatialIndex(segmentIndex, collectionName) {
        const segments = collectionName === "deviceSegments"
            ? state.deviceSegments : state.pipeSegments;
        const segment = segments[segmentIndex];
        const minimumCellX = spatialCellCoordinate(Math.min(segment.x1, segment.x2));
        const maximumCellX = spatialCellCoordinate(Math.max(segment.x1, segment.x2));
        const minimumCellY = spatialCellCoordinate(Math.min(segment.y1, segment.y2));
        const maximumCellY = spatialCellCoordinate(Math.max(segment.y1, segment.y2));
        const cellCount = (maximumCellX - minimumCellX + 1)
            * (maximumCellY - minimumCellY + 1);
        if (cellCount > 4096) {
            const globalCollection = collectionName === "deviceSegments"
                ? state.globalDeviceSegments : state.globalPipeSegments;
            globalCollection.push(segmentIndex);
            return;
        }

        for (let cellY = minimumCellY; cellY <= maximumCellY; ++cellY) {
            for (let cellX = minimumCellX; cellX <= maximumCellX; ++cellX)
                spatialCell(cellX, cellY)[collectionName].push(segmentIndex);
        }
    }

    function rebuildSpatialIndex() {
        state.spatialCells.clear();
        for (let index = 0; index < state.markers.length; ++index)
            addMarkerToSpatialIndex(index);
        for (let index = 0; index < state.deviceSegments.length; ++index)
            addSegmentToSpatialIndex(index, "deviceSegments");
        for (let index = 0; index < state.pipeSegments.length; ++index)
            addSegmentToSpatialIndex(index, "pipeSegments");
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

        state.markers = [];
        state.deviceSegments = [];
        state.pipeSegments = [];
        state.globalDeviceSegments = [];
        state.globalPipeSegments = [];
        state.spatialCells.clear();

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
            const projectedNode = {
                renderId: Number(node[0]),
                entityType: Number(node[1]),
                x: x,
                y: y
            };
            projectedNodes.push(projectedNode);
            nodesByRenderId.set(projectedNode.renderId, projectedNode);
            state.markers.push(projectedNode);
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

            if (vertices.length < 2)
                continue;

            const projectedLink = {
                renderId: Number(link[0]),
                entityType: Number(link[1]),
                vertices: vertices
            };
            projectedLinks.push(projectedLink);

            const segmentCollection = projectedLink.entityType === ENTITY_PIPE
                ? state.pipeSegments : state.deviceSegments;
            for (let index = 1; index < vertices.length; ++index) {
                segmentCollection.push({
                    renderId: projectedLink.renderId,
                    entityType: projectedLink.entityType,
                    x1: vertices[index - 1].x,
                    y1: vertices[index - 1].y,
                    x2: vertices[index].x,
                    y2: vertices[index].y
                });
            }

            if ((projectedLink.entityType === ENTITY_PUMP || projectedLink.entityType === ENTITY_VALVE)
                && vertices.length >= 3) {
                const center = vertices[Math.floor(vertices.length / 2)];
                state.markers.push({
                    renderId: projectedLink.renderId,
                    entityType: projectedLink.entityType,
                    x: center.x,
                    y: center.y
                });
            }
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
        for (const link of projectedLinks) {
            const vertices = link.vertices;
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
        rebuildSpatialIndex();
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

    function worldTransform(mapView) {
        const scale = scaleForZoom(mapView.zoom);
        const size = worldSize(mapView.zoom);
        let geometryOriginPixelX = state.geometryOriginX * scale;
        geometryOriginPixelX += Math.round(
            (mapView.centerPixelX - geometryOriginPixelX) / size) * size;

        const geometryOriginPixelY = state.geometryOriginY * scale;
        const tileOriginPixelX = mapView.originTileX * TILE_SIZE;
        const tileOriginPixelY = mapView.originTileY * TILE_SIZE;
        return {
            scale: scale,
            translateX: geometryOriginPixelX - tileOriginPixelX + mapView.translateX,
            translateY: geometryOriginPixelY - tileOriginPixelY + mapView.translateY
        };
    }

    function updateWorldTransform(mapView) {
        if (!state.worldGroup || !state.geometryReady)
            return;

        const transform = worldTransform(mapView);
        state.worldGroup.setAttribute(
            "transform", `translate(${transform.translateX} ${transform.translateY}) scale(${transform.scale})`);
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

    function pointToSegmentDistanceSquared(pointX, pointY, segment) {
        const segmentX = segment.x2 - segment.x1;
        const segmentY = segment.y2 - segment.y1;
        const lengthSquared = segmentX * segmentX + segmentY * segmentY;
        if (lengthSquared <= 0)
            return (pointX - segment.x1) ** 2 + (pointY - segment.y1) ** 2;

        const projection = ((pointX - segment.x1) * segmentX
            + (pointY - segment.y1) * segmentY) / lengthSquared;
        const bounded = Math.max(0, Math.min(1, projection));
        const nearestX = segment.x1 + bounded * segmentX;
        const nearestY = segment.y1 + bounded * segmentY;
        return (pointX - nearestX) ** 2 + (pointY - nearestY) ** 2;
    }

    function candidateIndices(pointX, pointY, radius, collectionName) {
        const minimumCellX = spatialCellCoordinate(pointX - radius);
        const maximumCellX = spatialCellCoordinate(pointX + radius);
        const minimumCellY = spatialCellCoordinate(pointY - radius);
        const maximumCellY = spatialCellCoordinate(pointY + radius);
        const result = new Set();

        const cellCount = (maximumCellX - minimumCellX + 1)
            * (maximumCellY - minimumCellY + 1);
        if (cellCount > 256) {
            const collection = collectionName === "markers" ? state.markers
                : collectionName === "deviceSegments" ? state.deviceSegments
                : state.pipeSegments;
            for (let index = 0; index < collection.length; ++index)
                result.add(index);
            return result;
        }

        for (let cellY = minimumCellY; cellY <= maximumCellY; ++cellY) {
            for (let cellX = minimumCellX; cellX <= maximumCellX; ++cellX) {
                const cell = state.spatialCells.get(spatialCellKey(cellX, cellY));
                if (!cell)
                    continue;
                for (const index of cell[collectionName])
                    result.add(index);
            }
        }

        if (collectionName === "deviceSegments") {
            for (const index of state.globalDeviceSegments)
                result.add(index);
        } else if (collectionName === "pipeSegments") {
            for (const index of state.globalPipeSegments)
                result.add(index);
        }
        return result;
    }

    function nearestMarkerHit(pointX, pointY, scale, markerHalfWidth) {
        const worldTolerance = markerHalfWidth / scale;
        const candidates = candidateIndices(pointX, pointY, worldTolerance, "markers");
        let bestHit = null;
        let bestDistanceSquared = Number.POSITIVE_INFINITY;

        for (const index of candidates) {
            const marker = state.markers[index];
            const deltaXScreen = (pointX - marker.x) * scale;
            const deltaYScreen = (pointY - marker.y) * scale;
            if (Math.abs(deltaXScreen) > markerHalfWidth
                || Math.abs(deltaYScreen) > markerHalfWidth) {
                continue;
            }

            const distanceSquared = deltaXScreen * deltaXScreen + deltaYScreen * deltaYScreen;
            if (distanceSquared >= bestDistanceSquared)
                continue;

            bestDistanceSquared = distanceSquared;
            bestHit = { renderId: marker.renderId, entityType: marker.entityType };
        }
        return bestHit;
    }

    function nearestSegmentHit(pointX, pointY, scale, collectionName) {
        const worldTolerance = LINK_HIT_DISTANCE / scale;
        const candidates = candidateIndices(pointX, pointY, worldTolerance, collectionName);
        const segments = collectionName === "deviceSegments"
            ? state.deviceSegments : state.pipeSegments;
        const maximumDistanceSquared = worldTolerance * worldTolerance;
        let bestHit = null;
        let bestDistanceSquared = maximumDistanceSquared;

        for (const index of candidates) {
            const segment = segments[index];
            const distanceSquared = pointToSegmentDistanceSquared(pointX, pointY, segment);
            if (distanceSquared > bestDistanceSquared)
                continue;

            bestDistanceSquared = distanceSquared;
            bestHit = { renderId: segment.renderId, entityType: segment.entityType };
        }
        return bestHit;
    }

    function hitTest(screenX, screenY) {
        const mapView = state.lastMapView;
        if (!state.geometryReady || !mapView || !mapView.topmost || !mapView.visible
            || !mapView.ready || !mapView.initialized) {
            return null;
        }

        if (!Number.isFinite(screenX) || !Number.isFinite(screenY)
            || screenX < 0 || screenY < 0 || screenX > mapView.width || screenY > mapView.height) {
            return null;
        }

        const transform = worldTransform(mapView);
        const pointX = state.geometryOriginX + (screenX - transform.translateX) / transform.scale;
        const pointY = state.geometryOriginY + (screenY - transform.translateY) / transform.scale;
        const markerHit = nearestMarkerHit(
            pointX, pointY, transform.scale, markerWidthForZoom(mapView.zoom) / 2);
        if (markerHit)
            return markerHit;

        const deviceHit = nearestSegmentHit(pointX, pointY, transform.scale, "deviceSegments");
        if (deviceHit)
            return deviceHit;

        return nearestSegmentHit(pointX, pointY, transform.scale, "pipeSegments");
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
        state.markers = [];
        state.deviceSegments = [];
        state.pipeSegments = [];
        state.globalDeviceSegments = [];
        state.globalPipeSegments = [];
        state.spatialCells.clear();
    }

    window.aowisBrowserNetwork = {
        setSnapshot: setSnapshot,
        hitTest: hitTest,
        destroy: destroy
    };

    initialize();
})();
