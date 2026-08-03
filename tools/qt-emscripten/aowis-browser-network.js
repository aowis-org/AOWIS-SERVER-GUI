(function () {
    "use strict";

    const TILE_SIZE = 256;
    const REFERENCE_ZOOM = 18;
    const NETWORK_COLOR = "#b000ff";
    const NETWORK_IMAGE_PADDING = 8;
    const LINK_HIT_DISTANCE = 7;
    const SPATIAL_CELL_SIZE = 128;
    const ENTITY_PIPE = 4;
    const ENTITY_PUMP = 5;
    const ENTITY_VALVE = 6;

    const state = {
        layer: null,
        image: null,
        imageObjectUrl: null,
        imageZoom: null,
        imageOffsetX: 0,
        imageOffsetY: 0,
        pendingImage: null,
        pendingImageObjectUrl: null,
        pendingImageZoom: null,
        imageGeneration: 0,
        unsubscribeView: null,
        geometryOriginX: 0,
        geometryOriginY: 0,
        geometryMinimumX: 0,
        geometryMinimumY: 0,
        geometryMaximumX: 0,
        geometryMaximumY: 0,
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
        spatialCells: new Map(),
        pointerMoveHandler: null,
        pointerLeaveHandler: null,
        pendingPointer: null,
        hoverFrameRequest: 0,
        cursorElement: null,
        cursorValue: "",
        cursorPriority: "",
        backgroundRed: 255,
        backgroundGreen: 255,
        backgroundBlue: 255,
        backgroundOpacity: 0,
        ownerId: 0
    };

    function applyBackground() {
        if (!state.layer)
            return;

        const alpha = Math.max(0, Math.min(100, state.backgroundOpacity)) / 100;
        state.layer.style.backgroundColor =
            `rgba(${state.backgroundRed}, ${state.backgroundGreen}, ${state.backgroundBlue}, ${alpha})`;
    }

    function ensureOverlay(mapLayer) {
        if (!mapLayer)
            return false;

        if (!state.layer) {
            state.layer = document.createElement("div");
            state.layer.id = "aowis-browser-network-layer";
            state.layer.setAttribute("aria-hidden", "true");
            state.layer.style.position = "absolute";
            state.layer.style.inset = "0";
            state.layer.style.display = "none";
            state.layer.style.pointerEvents = "none";
            state.layer.style.overflow = "hidden";
            state.layer.style.zIndex = "10";
            state.layer.style.contain = "strict";
            applyBackground();
        }

        if (state.layer.parentElement !== mapLayer)
            mapLayer.appendChild(state.layer);

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

    function devicePixelRatio() {
        return Math.max(1, window.devicePixelRatio || 1);
    }

    function snapToPhysicalPixel(value) {
        const ratio = devicePixelRatio();
        return Math.round(value * ratio) / ratio;
    }

    function revokeObjectUrl(objectUrl) {
        if (objectUrl)
            URL.revokeObjectURL(objectUrl);
    }

    function clearPendingImage() {
        ++state.imageGeneration;
        if (state.pendingImage)
            state.pendingImage.remove();
        revokeObjectUrl(state.pendingImageObjectUrl);
        state.pendingImage = null;
        state.pendingImageObjectUrl = null;
        state.pendingImageZoom = null;
    }

    function clearRenderedImage() {
        if (state.image)
            state.image.remove();
        revokeObjectUrl(state.imageObjectUrl);
        state.image = null;
        state.imageObjectUrl = null;
        state.imageZoom = null;
        state.imageOffsetX = 0;
        state.imageOffsetY = 0;
    }

    function clearNetworkImage() {
        clearPendingImage();
        clearRenderedImage();
    }

    function networkImageSpecification(zoom) {
        const scale = scaleForZoom(zoom);
        const minimumLocalX = state.geometryMinimumX - state.geometryOriginX;
        const minimumLocalY = state.geometryMinimumY - state.geometryOriginY;
        const maximumLocalX = state.geometryMaximumX - state.geometryOriginX;
        const maximumLocalY = state.geometryMaximumY - state.geometryOriginY;
        const scaledWidth = Math.max(0, (maximumLocalX - minimumLocalX) * scale);
        const scaledHeight = Math.max(0, (maximumLocalY - minimumLocalY) * scale);
        const width = Math.max(1, Math.ceil(scaledWidth + NETWORK_IMAGE_PADDING * 2));
        const height = Math.max(1, Math.ceil(scaledHeight + NETWORK_IMAGE_PADDING * 2));
        const translateX = NETWORK_IMAGE_PADDING - minimumLocalX * scale;
        const translateY = NETWORK_IMAGE_PADDING - minimumLocalY * scale;
        const svg = [
            `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}">`,
            `<g transform="translate(${formatted(translateX)} ${formatted(translateY)}) scale(${formatted(scale)})">`,
            `<path fill="none" stroke="${NETWORK_COLOR}" stroke-width="3" stroke-linecap="round" stroke-linejoin="round" vector-effect="non-scaling-stroke" d="${state.linkPathData}"/>`,
            `<path fill="none" stroke="${NETWORK_COLOR}" stroke-width="8" stroke-linecap="round" vector-effect="non-scaling-stroke" d="${state.nodePathData}"/>`,
            "</g></svg>"
        ].join("");
        return {
            svg: svg,
            width: width,
            height: height,
            offsetX: minimumLocalX * scale - NETWORK_IMAGE_PADDING,
            offsetY: minimumLocalY * scale - NETWORK_IMAGE_PADDING
        };
    }

    function ownsMapView(mapView) {
        return Boolean(mapView && state.ownerId !== 0 &&
            mapView.activeOwner === state.ownerId && mapView.topmost);
    }

    function shouldDisplayNetwork(mapView) {
        return Boolean(ownsMapView(mapView) && mapView.visible && mapView.ready
            && mapView.initialized && mapView.width > 0 && mapView.height > 0
            && state.geometryReady);
    }

    function shouldDisplayOverlay(mapView) {
        return Boolean(ownsMapView(mapView) && mapView.visible && mapView.ready
            && mapView.initialized && mapView.width > 0 && mapView.height > 0
            && (state.geometryReady || state.backgroundOpacity > 0));
    }

    function positionNetworkImage(mapView) {
        if (!state.image || state.imageZoom !== mapView.zoom)
            return;

        const transform = worldTransform(mapView);
        const x = snapToPhysicalPixel(transform.translateX + state.imageOffsetX);
        const y = snapToPhysicalPixel(transform.translateY + state.imageOffsetY);
        state.image.style.transform = `translate3d(${x}px, ${y}px, 0)`;
        state.image.style.display = shouldDisplayNetwork(mapView) ? "block" : "none";
    }

    function requestNetworkImage(zoom) {
        if (!state.layer || !state.geometryReady)
            return;
        if (state.image && state.imageZoom === zoom)
            return;
        if (state.pendingImage && state.pendingImageZoom === zoom)
            return;

        clearPendingImage();
        const generation = state.imageGeneration;
        const specification = networkImageSpecification(zoom);
        const objectUrl = URL.createObjectURL(
            new Blob([specification.svg], { type: "image/svg+xml" }));
        const image = document.createElement("img");
        image.setAttribute("aria-hidden", "true");
        image.alt = "";
        image.draggable = false;
        image.decoding = "async";
        image.style.position = "absolute";
        image.style.left = "0";
        image.style.top = "0";
        image.style.width = `${specification.width}px`;
        image.style.height = `${specification.height}px`;
        image.style.display = "none";
        image.style.pointerEvents = "none";
        image.style.transformOrigin = "0 0";
        image.style.willChange = "transform";
        image.style.backfaceVisibility = "hidden";

        state.pendingImage = image;
        state.pendingImageObjectUrl = objectUrl;
        state.pendingImageZoom = zoom;

        image.addEventListener("load", () => {
            if (generation !== state.imageGeneration || state.pendingImage !== image) {
                revokeObjectUrl(objectUrl);
                return;
            }
            if (!state.lastMapView || state.lastMapView.zoom !== zoom) {
                state.pendingImage = null;
                state.pendingImageObjectUrl = null;
                state.pendingImageZoom = null;
                revokeObjectUrl(objectUrl);
                return;
            }

            clearRenderedImage();
            state.pendingImage = null;
            state.pendingImageObjectUrl = null;
            state.pendingImageZoom = null;
            state.image = image;
            state.imageObjectUrl = objectUrl;
            state.imageZoom = zoom;
            state.imageOffsetX = specification.offsetX;
            state.imageOffsetY = specification.offsetY;
            state.layer.appendChild(image);
            if (state.lastMapView)
                positionNetworkImage(state.lastMapView);
        }, { once: true });

        image.addEventListener("error", () => {
            if (state.pendingImage === image) {
                state.pendingImage = null;
                state.pendingImageObjectUrl = null;
                state.pendingImageZoom = null;
            }
            revokeObjectUrl(objectUrl);
            console.error("Failed to rasterize AOWIS browser network SVG");
        }, { once: true });

        image.src = objectUrl;
    }

    function validCoordinate(coordinate) {
        return Array.isArray(coordinate) && coordinate.length >= 2
            && Number.isFinite(Number(coordinate[0]))
            && Number.isFinite(Number(coordinate[1]));
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
        clearNetworkImage();
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
            state.geometryMinimumX = 0;
            state.geometryMinimumY = 0;
            state.geometryMaximumX = 0;
            state.geometryMaximumY = 0;
            state.linkPathData = "";
            state.nodePathData = "";
            state.geometryReady = false;
            return;
        }

        state.geometryOriginX = (minimumX + maximumX) / 2;
        state.geometryOriginY = (minimumY + maximumY) / 2;
        state.geometryMinimumX = minimumX;
        state.geometryMinimumY = minimumY;
        state.geometryMaximumX = maximumX;
        state.geometryMaximumY = maximumY;

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
    }

    function updateViewport(width, height) {
        state.width = width;
        state.height = height;
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

    function handleMapViewChanged(mapView) {
        state.lastMapView = mapView;

        if (!mapView || !ensureOverlay(mapView.layer)) {
            if (state.layer)
                state.layer.style.display = "none";
            return;
        }

        updateViewport(mapView.width, mapView.height);
        const shouldShow = shouldDisplayOverlay(mapView);
        state.layer.style.display = shouldShow ? "block" : "none";
        if (!shouldShow)
            return;

        if (!state.geometryReady)
            return;

        if (!state.image || state.imageZoom !== mapView.zoom) {
            if (state.image)
                state.image.style.display = "none";
            requestNetworkImage(mapView.zoom);
            return;
        }

        if (state.pendingImage && state.pendingImageZoom !== mapView.zoom)
            clearPendingImage();
        positionNetworkImage(mapView);
    }

    function initialize() {
        if (!window.aowisBrowserMap || typeof window.aowisBrowserMap.subscribeView !== "function")
            throw new Error("AOWIS browser network requires aowis-browser-map.js");

        state.unsubscribeView = window.aowisBrowserMap.subscribeView(handleMapViewChanged);
        state.pointerMoveHandler = handlePointerMove;
        state.pointerLeaveHandler = clearHoverCursor;
        window.addEventListener("pointermove", state.pointerMoveHandler, { capture: true, passive: true });
        window.addEventListener("pointerleave", state.pointerLeaveHandler, { capture: true, passive: true });
        window.addEventListener("blur", state.pointerLeaveHandler, { passive: true });
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
        if (!state.geometryReady || !ownsMapView(mapView) || !mapView.visible
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

    function restoreCursorElement() {
        if (!state.cursorElement)
            return;

        if (state.cursorValue)
            state.cursorElement.style.setProperty(
                "cursor", state.cursorValue, state.cursorPriority);
        else
            state.cursorElement.style.removeProperty("cursor");

        state.cursorElement = null;
        state.cursorValue = "";
        state.cursorPriority = "";
    }

    function setPointerCursor(element) {
        if (state.cursorElement === element)
            return;

        restoreCursorElement();
        if (!element)
            return;

        state.cursorElement = element;
        state.cursorValue = element.style.getPropertyValue("cursor");
        state.cursorPriority = element.style.getPropertyPriority("cursor");
        element.style.setProperty("cursor", "pointer", "important");
    }

    function pointerTarget(clientX, clientY, mapView) {
        const root = mapView.layer ? mapView.layer.getRootNode() : null;
        if (!root || typeof root.elementFromPoint !== "function")
            return null;

        const element = root.elementFromPoint(clientX, clientY);
        if (!(element instanceof Element))
            return null;

        const windowElement = element.closest(".qt-decorated-window");
        if (windowElement) {
            const mapZIndex = Number.parseInt(window.getComputedStyle(mapView.layer).zIndex, 10) || 0;
            const windowZIndex = Number.parseInt(window.getComputedStyle(windowElement).zIndex, 10) || 0;
            if (windowZIndex > mapZIndex)
                return null;
        }

        return element;
    }

    function updateHoverCursor() {
        state.hoverFrameRequest = 0;
        const pointer = state.pendingPointer;
        const mapView = state.lastMapView;
        if (!pointer || pointer.buttons !== 0 || !mapView || !mapView.layer
            || !ownsMapView(mapView) || !mapView.visible || !mapView.ready || !mapView.initialized
            || !state.geometryReady) {
            restoreCursorElement();
            return;
        }

        const rect = mapView.layer.getBoundingClientRect();
        const screenX = pointer.clientX - rect.left;
        const screenY = pointer.clientY - rect.top;
        if (!hitTest(screenX, screenY)) {
            restoreCursorElement();
            return;
        }

        setPointerCursor(pointerTarget(pointer.clientX, pointer.clientY, mapView));
    }

    function scheduleHoverCursorUpdate() {
        if (state.hoverFrameRequest !== 0)
            return;
        state.hoverFrameRequest = window.requestAnimationFrame(updateHoverCursor);
    }

    function handlePointerMove(event) {
        state.pendingPointer = {
            clientX: event.clientX,
            clientY: event.clientY,
            buttons: event.buttons
        };
        scheduleHoverCursorUpdate();
    }

    function clearHoverCursor() {
        state.pendingPointer = null;
        if (state.hoverFrameRequest !== 0) {
            window.cancelAnimationFrame(state.hoverFrameRequest);
            state.hoverFrameRequest = 0;
        }
        restoreCursorElement();
    }

    // nodes: [renderId, entityType, uuid, hydraulicId, longitude, latitude]
    // links: [renderId, entityType, uuid, hydraulicId, startNodeId, endNodeId, vertices]
    function setSnapshot(snapshot) {
        if (!snapshot || !Array.isArray(snapshot.nodes) || !Array.isArray(snapshot.links))
            throw new TypeError("Invalid AOWIS browser network snapshot");

        clearHoverCursor();
        state.snapshot = snapshot;
        buildGeometry(snapshot);
        if (state.lastMapView)
            handleMapViewChanged(state.lastMapView);
    }

    function setBackground(red, green, blue, opacity) {
        state.backgroundRed = Math.max(0, Math.min(255, Number(red) || 0));
        state.backgroundGreen = Math.max(0, Math.min(255, Number(green) || 0));
        state.backgroundBlue = Math.max(0, Math.min(255, Number(blue) || 0));
        state.backgroundOpacity = Math.max(0, Math.min(100, Number(opacity) || 0));
        applyBackground();
        if (state.lastMapView)
            handleMapViewChanged(state.lastMapView);
    }

    function setOwnerId(ownerId) {
        state.ownerId = Number(ownerId) | 0;
        clearHoverCursor();
        if (state.lastMapView)
            handleMapViewChanged(state.lastMapView);
    }

    function destroy() {
        if (state.unsubscribeView)
            state.unsubscribeView();
        state.unsubscribeView = null;

        if (state.pointerMoveHandler)
            window.removeEventListener("pointermove", state.pointerMoveHandler, true);
        if (state.pointerLeaveHandler) {
            window.removeEventListener("pointerleave", state.pointerLeaveHandler, true);
            window.removeEventListener("blur", state.pointerLeaveHandler);
        }
        state.pointerMoveHandler = null;
        state.pointerLeaveHandler = null;
        clearHoverCursor();

        clearNetworkImage();
        if (state.layer)
            state.layer.remove();
        state.layer = null;
        state.geometryOriginX = 0;
        state.geometryOriginY = 0;
        state.geometryMinimumX = 0;
        state.geometryMinimumY = 0;
        state.geometryMaximumX = 0;
        state.geometryMaximumY = 0;
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
        setBackground: setBackground,
        setOwnerId: setOwnerId,
        hitTest: hitTest,
        destroy: destroy
    };

    initialize();
})();
