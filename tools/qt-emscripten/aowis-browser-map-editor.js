(function () {
    "use strict";

    const REFERENCE_ZOOM = 18;
    const STATIC_PADDING = 64;
    const MAX_STATIC_DIMENSION = 16384;
    const MAX_STATIC_AREA = 64 * 1024 * 1024;
    const MARKER_DOT_RADIUS = 5;
    const CONNECTION_TARGET_RADIUS = 9;
    const PIPE_VERTEX_RADIUS = 4;
    const ENTITY_PIPE = 4;
    const ENTITY_PUMP = 5;
    const ENTITY_VALVE = 6;
    const SELECTED_COLOR = "rgb(0, 190, 255)";
    const PREVIEW_COLOR = "rgb(0, 140, 255)";
    const DEVICE_LINK_COLOR = "rgb(139, 90, 43)";

    const glowOffsets = [
        [-6, 0], [6, 0], [0, -6], [0, 6], [-5, -3], [-5, 3], [5, -3], [5, 3],
        [-3, -5], [-3, 5], [3, -5], [3, 5], [-4, 0], [4, 0], [0, -4], [0, 4],
        [-3, -3], [-3, 3], [3, -3], [3, 3], [-2, 0], [2, 0], [0, -2], [0, 2],
        [-2, -2], [-2, 2], [2, -2], [2, 2]
    ];

    const iconPaths = new Map([
        [1, "map-editor-icons/junction.png"],
        [2, "map-editor-icons/reservoir.png"],
        [3, "map-editor-icons/tower.png"],
        [4, "map-editor-icons/pipe.png"],
        [5, "map-editor-icons/pump.png"],
        [6, "map-editor-icons/valve.png"],
        [7, "map-editor-icons/customer.png"],
        [8, "map-editor-icons/electricity.png"],
        [9, "map-editor-icons/energy.png"],
        [10, "map-editor-icons/energy.png"],
        [11, "map-editor-icons/energy.png"],
        [12, "map-editor-icons/energy.png"],
        [13, "map-editor-icons/energy.png"],
        [14, "map-editor-icons/electricity.png"],
        [15, "map-editor-icons/electricity.png"],
        [16, "map-editor-icons/electricity.png"],
        [17, "map-editor-icons/electricity.png"],
        [18, "map-editor-icons/geomarker.png"]
    ]);

    const state = {
        layer: null,
        underlayCanvas: null,
        staticCanvas: null,
        dynamicCanvas: null,
        networkSnapshot: null,
        visualState: defaultVisualState(),
        viewportState: defaultViewportState(),
        lastMapView: null,
        unsubscribeView: null,
        nodes: [],
        links: [],
        nodesByUuid: new Map(),
        geometryReady: false,
        geometryMinimumX: 0,
        geometryMinimumY: 0,
        geometryMaximumX: 0,
        geometryMaximumY: 0,
        geometryWrapReferenceLongitude: Number.NaN,
        staticZoom: null,
        staticEntityWidth: 0,
        staticOriginX: 0,
        staticOriginY: 0,
        staticCssWidth: 0,
        staticCssHeight: 0,
        staticViewportFallback: false,
        staticRenderPending: false,
        underlayRenderPending: false,
        dynamicRenderPending: false,
        icons: new Map(),
        tintedIcons: new Map(),
        backgroundRed: 255,
        backgroundGreen: 255,
        backgroundBlue: 255
    };

    function defaultVisualState() {
        return {
            revision: "0",
            selectedMarkerUuids: [],
            selectedPipeUuids: [],
            wrapReferenceLongitude: 0,
            entityWidth: 10,
            placement: {
                creating: false,
                floatingMarkerVisible: false,
                entity: 0,
                mouseX: 0,
                mouseY: 0,
                connectionTargetUuid: "",
                pipeStartNodeUuid: "",
                pipeIntermediateVertices: [],
                deviceLinkStartNodeUuid: "",
                floatingWidth: 0
            }
        };
    }

    function defaultViewportState() {
        return {
            backgroundOpacity: 0,
            tileSelection: { visible: false, xMin: 0, xMax: -1, yMin: 0, yMax: -1 },
            rectangleSelection: { visible: false, x: 0, y: 0, width: 0, height: 0 }
        };
    }

    function projection() {
        if (!window.aowisBrowserMap || !window.aowisBrowserMap.projection)
            throw new Error("AOWIS browser map editor requires map projection helpers");
        return window.aowisBrowserMap.projection;
    }

    function devicePixelRatio() {
        return projection().devicePixelRatio();
    }

    function createCanvas(zIndex) {
        const canvas = document.createElement("canvas");
        canvas.setAttribute("aria-hidden", "true");
        canvas.style.position = "absolute";
        canvas.style.left = "0";
        canvas.style.top = "0";
        canvas.style.pointerEvents = "none";
        canvas.style.zIndex = String(zIndex);
        canvas.style.transformOrigin = "0 0";
        return canvas;
    }

    function ensureLayer(mapLayer) {
        if (!mapLayer)
            return false;

        if (!state.layer) {
            state.layer = document.createElement("div");
            state.layer.id = "aowis-browser-map-editor-layer";
            state.layer.setAttribute("aria-hidden", "true");
            state.layer.style.position = "absolute";
            state.layer.style.inset = "0";
            state.layer.style.display = "none";
            state.layer.style.pointerEvents = "none";
            state.layer.style.overflow = "hidden";
            state.layer.style.zIndex = "10";
            state.layer.style.contain = "strict";

            state.underlayCanvas = createCanvas(1);
            state.staticCanvas = createCanvas(2);
            state.staticCanvas.style.willChange = "transform";
            state.dynamicCanvas = createCanvas(3);
            state.layer.appendChild(state.underlayCanvas);
            state.layer.appendChild(state.staticCanvas);
            state.layer.appendChild(state.dynamicCanvas);
            applyBackground();
        }

        if (state.layer.parentElement !== mapLayer)
            mapLayer.appendChild(state.layer);
        return true;
    }

    function shouldDisplay(mapView) {
        return Boolean(mapView && !mapView.topmost && mapView.visible && mapView.ready &&
            mapView.initialized && mapView.width > 0 && mapView.height > 0);
    }

    function resizeViewportCanvas(canvas, width, height) {
        if (!canvas)
            return false;

        const ratio = devicePixelRatio();
        const physicalWidth = Math.max(1, Math.ceil(width * ratio));
        const physicalHeight = Math.max(1, Math.ceil(height * ratio));
        const changed = canvas.width !== physicalWidth || canvas.height !== physicalHeight;
        if (changed) {
            canvas.width = physicalWidth;
            canvas.height = physicalHeight;
            canvas.style.width = `${width}px`;
            canvas.style.height = `${height}px`;
        }
        return changed;
    }

    function viewportContext(canvas, width, height) {
        resizeViewportCanvas(canvas, width, height);
        const context = canvas.getContext("2d");
        const ratio = devicePixelRatio();
        context.setTransform(ratio, 0, 0, ratio, 0, 0);
        context.clearRect(0, 0, width, height);
        return context;
    }

    function applyBackground() {
        if (!state.layer)
            return;
        const opacity = Math.max(0, Math.min(100, Number(state.viewportState.backgroundOpacity) || 0));
        state.layer.style.backgroundColor = `rgba(${state.backgroundRed}, ${state.backgroundGreen}, ${state.backgroundBlue}, ${opacity / 100})`;
    }

    function normalizeUuid(value) {
        return typeof value === "string" ? value.toLowerCase() : "";
    }

    function selectedSet(values) {
        const result = new Set();
        if (!Array.isArray(values))
            return result;
        for (const value of values)
            result.add(normalizeUuid(value));
        return result;
    }

    function worldPoint(longitude, latitude, wrapReferenceLongitude) {
        const mapProjection = projection();
        const referenceX = mapProjection.longitudeToWorldPixel(
            wrapReferenceLongitude, REFERENCE_ZOOM);
        const rawX = mapProjection.longitudeToWorldPixel(longitude, REFERENCE_ZOOM);
        return {
            x: mapProjection.nearestWrappedWorldPixel(rawX, referenceX, REFERENCE_ZOOM),
            y: mapProjection.latitudeToWorldPixel(latitude, REFERENCE_ZOOM)
        };
    }

    function parseNetworkGeometry() {
        state.nodes = [];
        state.links = [];
        state.nodesByUuid.clear();
        state.geometryReady = false;

        const snapshot = state.networkSnapshot;
        if (!snapshot || !Array.isArray(snapshot.nodes) || !Array.isArray(snapshot.links))
            return;

        const wrapReference = Number(state.visualState.wrapReferenceLongitude) || 0;
        let minimumX = Number.POSITIVE_INFINITY;
        let minimumY = Number.POSITIVE_INFINITY;
        let maximumX = Number.NEGATIVE_INFINITY;
        let maximumY = Number.NEGATIVE_INFINITY;

        function include(point) {
            minimumX = Math.min(minimumX, point.x);
            minimumY = Math.min(minimumY, point.y);
            maximumX = Math.max(maximumX, point.x);
            maximumY = Math.max(maximumY, point.y);
        }

        for (const rawNode of snapshot.nodes) {
            if (!Array.isArray(rawNode) || rawNode.length < 6)
                continue;
            const point = worldPoint(Number(rawNode[4]), Number(rawNode[5]), wrapReference);
            const node = {
                renderId: Number(rawNode[0]) >>> 0,
                entityType: Number(rawNode[1]) | 0,
                uuid: normalizeUuid(rawNode[2]),
                id: String(rawNode[3] || ""),
                longitude: Number(rawNode[4]),
                latitude: Number(rawNode[5]),
                x: point.x,
                y: point.y
            };
            state.nodes.push(node);
            state.nodesByUuid.set(node.uuid, node);
            include(point);
        }

        for (const rawLink of snapshot.links) {
            if (!Array.isArray(rawLink) || rawLink.length < 7 || !Array.isArray(rawLink[6]))
                continue;
            const vertices = [];
            for (const rawVertex of rawLink[6]) {
                if (!Array.isArray(rawVertex) || rawVertex.length < 2)
                    continue;
                const longitude = Number(rawVertex[0]);
                const latitude = Number(rawVertex[1]);
                const point = worldPoint(longitude, latitude, wrapReference);
                vertices.push({ longitude: longitude, latitude: latitude, x: point.x, y: point.y });
                include(point);
            }
            state.links.push({
                renderId: Number(rawLink[0]) >>> 0,
                entityType: Number(rawLink[1]) | 0,
                uuid: normalizeUuid(rawLink[2]),
                id: String(rawLink[3] || ""),
                startNodeRenderId: Number(rawLink[4]) >>> 0,
                endNodeRenderId: Number(rawLink[5]) >>> 0,
                vertices: vertices
            });
        }

        state.geometryWrapReferenceLongitude = wrapReference;
        if (!Number.isFinite(minimumX)) {
            state.geometryMinimumX = 0;
            state.geometryMinimumY = 0;
            state.geometryMaximumX = 0;
            state.geometryMaximumY = 0;
            return;
        }

        state.geometryMinimumX = minimumX;
        state.geometryMinimumY = minimumY;
        state.geometryMaximumX = maximumX;
        state.geometryMaximumY = maximumY;
        state.geometryReady = true;
    }

    function iconPath(entityType) {
        return iconPaths.get(entityType) || "map-editor-icons/geomarker.png";
    }

    function requestIcon(entityType) {
        const path = iconPath(entityType);
        let entry = state.icons.get(path);
        if (entry)
            return entry;

        const image = new Image();
        entry = { image: image, loaded: false, failed: false };
        state.icons.set(path, entry);
        image.decoding = "async";
        image.onload = function () {
            entry.loaded = true;
            state.tintedIcons.clear();
            invalidateStaticCache();
            scheduleDynamicRender();
        };
        image.onerror = function () {
            entry.failed = true;
            console.error(`Could not load map editor icon: ${path}`);
        };
        image.src = path;
        return entry;
    }

    function iconDimensions(entityType, width) {
        const entry = requestIcon(entityType);
        if (!entry.loaded || entry.image.naturalWidth <= 0)
            return null;
        return {
            width: width,
            height: entry.image.naturalHeight * width / entry.image.naturalWidth,
            image: entry.image
        };
    }

    function tintedIcon(entityType, width) {
        const key = `${entityType}|${width}`;
        const cached = state.tintedIcons.get(key);
        if (cached)
            return cached;

        const dimensions = iconDimensions(entityType, width);
        if (!dimensions)
            return null;
        const ratio = devicePixelRatio();
        const canvas = document.createElement("canvas");
        canvas.width = Math.max(1, Math.ceil(dimensions.width * ratio));
        canvas.height = Math.max(1, Math.ceil(dimensions.height * ratio));
        const context = canvas.getContext("2d");
        context.setTransform(ratio, 0, 0, ratio, 0, 0);
        context.drawImage(dimensions.image, 0, 0, dimensions.width, dimensions.height);
        context.globalCompositeOperation = "source-in";
        context.fillStyle = SELECTED_COLOR;
        context.fillRect(0, 0, dimensions.width, dimensions.height);
        context.globalCompositeOperation = "source-over";
        state.tintedIcons.set(key, canvas);
        return canvas;
    }

    function drawIcon(context, entityType, width, x, y, centered, selected) {
        const dimensions = iconDimensions(entityType, width);
        if (!dimensions)
            return;

        const left = centered ? x - dimensions.width / 2 : Math.round(x);
        const top = centered ? y - dimensions.height / 2 : Math.round(y) - dimensions.height;
        if (selected) {
            const glow = tintedIcon(entityType, width);
            if (glow) {
                context.save();
                context.globalAlpha = 0.8;
                for (const offset of glowOffsets)
                    context.drawImage(glow, left + offset[0], top + offset[1], dimensions.width, dimensions.height);
                context.restore();
            }
        }
        context.drawImage(dimensions.image, left, top, dimensions.width, dimensions.height);
    }

    function linkCenter(link) {
        if (link.vertices.length > 2)
            return link.vertices[1];
        if (link.vertices.length < 2)
            return null;

        const start = link.vertices[0];
        const end = link.vertices[link.vertices.length - 1];
        const mapProjection = projection();
        const longitudeDelta = mapProjection.normalizeLongitude(end.longitude - start.longitude);
        const longitude = mapProjection.normalizeLongitude(start.longitude + longitudeDelta / 2);
        const latitude = (start.latitude + end.latitude) / 2;
        return worldPoint(longitude, latitude, state.visualState.wrapReferenceLongitude);
    }

    function isDeviceLink(entityType) {
        return entityType === ENTITY_PUMP || entityType === ENTITY_VALVE;
    }

    function invalidateStaticCache() {
        state.staticZoom = null;
        state.staticEntityWidth = 0;
        scheduleStaticRender();
    }

    function staticSpecification(mapView) {
        const scale = Math.pow(2, mapView.zoom - REFERENCE_ZOOM);
        const width = Math.max(1, (state.geometryMaximumX - state.geometryMinimumX) * scale + STATIC_PADDING * 2);
        const height = Math.max(1, (state.geometryMaximumY - state.geometryMinimumY) * scale + STATIC_PADDING * 2);
        const ratio = devicePixelRatio();
        const physicalWidth = width * ratio;
        const physicalHeight = height * ratio;
        const fallback = physicalWidth > MAX_STATIC_DIMENSION || physicalHeight > MAX_STATIC_DIMENSION ||
            physicalWidth * physicalHeight > MAX_STATIC_AREA;
        if (fallback) {
            return {
                fallback: true,
                width: mapView.width,
                height: mapView.height,
                originX: 0,
                originY: 0,
                scale: scale
            };
        }
        return {
            fallback: false,
            width: Math.ceil(width),
            height: Math.ceil(height),
            originX: state.geometryMinimumX - STATIC_PADDING / scale,
            originY: state.geometryMinimumY - STATIC_PADDING / scale,
            scale: scale
        };
    }

    function configureStaticCanvas(specification) {
        const ratio = devicePixelRatio();
        const physicalWidth = Math.max(1, Math.ceil(specification.width * ratio));
        const physicalHeight = Math.max(1, Math.ceil(specification.height * ratio));
        state.staticCanvas.width = physicalWidth;
        state.staticCanvas.height = physicalHeight;
        state.staticCanvas.style.width = `${specification.width}px`;
        state.staticCanvas.style.height = `${specification.height}px`;
        state.staticCssWidth = specification.width;
        state.staticCssHeight = specification.height;
        state.staticOriginX = specification.originX;
        state.staticOriginY = specification.originY;
        state.staticViewportFallback = specification.fallback;
        const context = state.staticCanvas.getContext("2d");
        context.setTransform(ratio, 0, 0, ratio, 0, 0);
        context.clearRect(0, 0, specification.width, specification.height);
        context.imageSmoothingEnabled = true;
        return context;
    }

    function cachedPoint(point, specification, mapView) {
        if (specification.fallback)
            return screenPoint(point, mapView);
        return {
            x: (point.x - specification.originX) * specification.scale,
            y: (point.y - specification.originY) * specification.scale
        };
    }

    function strokePolyline(context, vertices, pointFunction, color, width) {
        if (vertices.length < 2)
            return;
        context.beginPath();
        let point = pointFunction(vertices[0]);
        context.moveTo(point.x, point.y);
        for (let index = 1; index < vertices.length; ++index) {
            point = pointFunction(vertices[index]);
            context.lineTo(point.x, point.y);
        }
        context.strokeStyle = color;
        context.lineWidth = width;
        context.lineCap = "round";
        context.lineJoin = "round";
        context.stroke();
    }

    function renderStaticNetwork() {
        state.staticRenderPending = false;
        const mapView = state.lastMapView;
        if (!state.staticCanvas || !state.geometryReady || !shouldDisplay(mapView)) {
            if (state.staticCanvas)
                state.staticCanvas.style.display = "none";
            return;
        }

        const specification = staticSpecification(mapView);
        const context = configureStaticCanvas(specification);
        const pointFunction = (point) => cachedPoint(point, specification, mapView);
        const entityWidth = Math.max(1, Number(state.visualState.entityWidth) || 10);

        for (const link of state.links) {
            if (link.entityType !== ENTITY_PIPE)
                continue;
            strokePolyline(context, link.vertices, pointFunction, "black", 3);
            context.fillStyle = "black";
            for (let index = 1; index + 1 < link.vertices.length; ++index) {
                const point = pointFunction(link.vertices[index]);
                context.beginPath();
                context.arc(point.x, point.y, PIPE_VERTEX_RADIUS, 0, Math.PI * 2);
                context.fill();
            }
        }

        for (const link of state.links) {
            if (!isDeviceLink(link.entityType) || link.vertices.length < 2)
                continue;
            const center = linkCenter(link);
            if (!center)
                continue;
            const start = pointFunction(link.vertices[0]);
            const middle = pointFunction(center);
            const end = pointFunction(link.vertices[link.vertices.length - 1]);
            context.beginPath();
            context.moveTo(start.x, start.y);
            context.lineTo(middle.x, middle.y);
            context.lineTo(end.x, end.y);
            context.strokeStyle = DEVICE_LINK_COLOR;
            context.lineWidth = 3;
            context.lineCap = "round";
            context.lineJoin = "round";
            context.stroke();
        }

        for (const node of state.nodes) {
            const point = pointFunction(node);
            context.beginPath();
            context.arc(point.x, point.y, MARKER_DOT_RADIUS, 0, Math.PI * 2);
            context.fillStyle = "black";
            context.fill();
        }

        for (const link of state.links) {
            if (!isDeviceLink(link.entityType) || link.vertices.length < 2)
                continue;
            const center = linkCenter(link);
            if (!center)
                continue;
            const point = pointFunction(center);
            drawIcon(context, link.entityType, entityWidth, point.x, point.y, true, false);
        }

        for (const node of state.nodes) {
            const point = pointFunction(node);
            drawIcon(context, node.entityType, entityWidth, point.x, point.y, false, false);
        }

        state.staticZoom = mapView.zoom;
        state.staticEntityWidth = entityWidth;
        state.staticCanvas.style.display = "block";
        positionStaticCanvas(mapView);
    }

    function positionStaticCanvas(mapView) {
        if (!state.staticCanvas || state.staticZoom !== mapView.zoom)
            return;
        if (state.staticViewportFallback) {
            state.staticCanvas.style.transform = "translate3d(0, 0, 0)";
            return;
        }

        const scale = Math.pow(2, mapView.zoom - REFERENCE_ZOOM);
        const originAtZoom = worldXAtMapZoom(state.staticOriginX, mapView);
        const left = projection().snapToPhysicalPixel(
            mapView.width / 2 + originAtZoom - mapView.centerPixelX);
        const top = projection().snapToPhysicalPixel(
            mapView.height / 2 + state.staticOriginY * scale - mapView.centerPixelY);
        state.staticCanvas.style.transform = `translate3d(${left}px, ${top}px, 0)`;
    }

    function scheduleStaticRender() {
        if (state.staticRenderPending)
            return;
        state.staticRenderPending = true;
        window.requestAnimationFrame(renderStaticNetwork);
    }

    function worldXAtMapZoom(referenceZoomX, mapView) {
        const mapProjection = projection();
        const scale = Math.pow(2, mapView.zoom - REFERENCE_ZOOM);
        const wrapReferenceX = mapProjection.longitudeToWorldPixel(
            state.visualState.wrapReferenceLongitude, REFERENCE_ZOOM);
        const wrappedReferenceX = mapProjection.nearestWrappedWorldPixel(
            wrapReferenceX * scale, mapView.centerPixelX, mapView.zoom);
        return wrappedReferenceX + (referenceZoomX - wrapReferenceX) * scale;
    }

    function screenPoint(point, mapView) {
        const scale = Math.pow(2, mapView.zoom - REFERENCE_ZOOM);
        return {
            x: mapView.width / 2 + worldXAtMapZoom(point.x, mapView) - mapView.centerPixelX,
            y: mapView.height / 2 + point.y * scale - mapView.centerPixelY
        };
    }

    function screenFromCoordinate(coordinate, mapView) {
        if (!Array.isArray(coordinate) || coordinate.length < 2)
            return null;
        return screenPoint(worldPoint(
            Number(coordinate[0]), Number(coordinate[1]),
            state.visualState.wrapReferenceLongitude), mapView);
    }

    function drawTileSelection(context, mapView) {
        const tile = state.viewportState.tileSelection;
        if (!tile || !tile.visible || tile.xMax < tile.xMin || tile.yMax < tile.yMin)
            return;

        const tileSize = projection().tileSize;
        const worldTileCount = Math.pow(2, mapView.zoom);
        const centerTileX = mapView.centerPixelX / tileSize;
        const centerTileY = mapView.centerPixelY / tileSize;
        let westTile = Number(tile.xMin);
        let eastTile = Number(tile.xMax) + 1;
        const northTile = Number(tile.yMin);
        const southTile = Number(tile.yMax) + 1;
        const selectionCenter = (westTile + eastTile) / 2;
        const wrapShift = Math.round((centerTileX - selectionCenter) / worldTileCount) * worldTileCount;
        westTile += wrapShift;
        eastTile += wrapShift;

        const left = mapView.width / 2 + (westTile - centerTileX) * tileSize;
        const right = mapView.width / 2 + (eastTile - centerTileX) * tileSize;
        const top = mapView.height / 2 + (northTile - centerTileY) * tileSize;
        const bottom = mapView.height / 2 + (southTile - centerTileY) * tileSize;
        const width = right - left;
        const height = bottom - top;
        if (width <= 0 || height <= 0 || right < 0 || bottom < 0 || left > mapView.width || top > mapView.height)
            return;

        const fill = context.createLinearGradient(left, top, right, bottom);
        fill.addColorStop(0, "rgba(92, 255, 82, 0.212)");
        fill.addColorStop(0.5, "rgba(32, 224, 58, 0.259)");
        fill.addColorStop(1, "rgba(8, 132, 38, 0.298)");
        context.fillStyle = fill;
        context.fillRect(left, top, width, height);

        function strokeRect(color, lineWidth) {
            context.strokeStyle = color;
            context.lineWidth = lineWidth;
            context.lineJoin = "miter";
            context.strokeRect(left, top, width, height);
        }
        strokeRect("rgba(60, 255, 78, 0.212)", 12);
        strokeRect("rgba(92, 255, 96, 0.471)", 5);
        strokeRect("rgba(155, 255, 145, 0.902)", 1.5);

        context.strokeStyle = "rgba(104, 255, 104, 0.412)";
        context.lineWidth = 1;
        const viewportWestTile = centerTileX - mapView.width / 2 / tileSize;
        const viewportEastTile = centerTileX + mapView.width / 2 / tileSize;
        const firstVisibleX = Math.max(Number(tile.xMin) + 1, Math.ceil(viewportWestTile - wrapShift));
        const lastVisibleX = Math.min(Number(tile.xMax), Math.floor(viewportEastTile - wrapShift));
        for (let tileX = firstVisibleX; tileX <= lastVisibleX; ++tileX) {
            const x = mapView.width / 2 + (tileX + wrapShift - centerTileX) * tileSize;
            context.beginPath();
            context.moveTo(x, top);
            context.lineTo(x, bottom);
            context.stroke();
        }

        const viewportNorthTile = centerTileY - mapView.height / 2 / tileSize;
        const viewportSouthTile = centerTileY + mapView.height / 2 / tileSize;
        const firstVisibleY = Math.max(Number(tile.yMin) + 1, Math.ceil(viewportNorthTile));
        const lastVisibleY = Math.min(Number(tile.yMax), Math.floor(viewportSouthTile));
        for (let tileY = firstVisibleY; tileY <= lastVisibleY; ++tileY) {
            const y = mapView.height / 2 + (tileY - centerTileY) * tileSize;
            context.beginPath();
            context.moveTo(left, y);
            context.lineTo(right, y);
            context.stroke();
        }
    }

    function roundedRectPath(context, x, y, width, height, radius) {
        const boundedRadius = Math.max(0, Math.min(radius, width / 2, height / 2));
        context.beginPath();
        context.roundRect(x, y, width, height, boundedRadius);
    }

    function drawRectangleSelection(context) {
        const rectangle = state.viewportState.rectangleSelection;
        if (!rectangle || !rectangle.visible || rectangle.width <= 0 || rectangle.height <= 0)
            return;

        const x = Number(rectangle.x) + 2.5;
        const y = Number(rectangle.y) + 2.5;
        const width = Number(rectangle.width) - 5;
        const height = Number(rectangle.height) - 5;
        if (width <= 0 || height <= 0)
            return;
        const radius = Math.min(2, Math.min(width, height) / 8);

        const fill = context.createLinearGradient(x, y, x, y + height);
        fill.addColorStop(0, "rgba(35, 151, 211, 0.094)");
        fill.addColorStop(0.45, "rgba(0, 145, 215, 0.125)");
        fill.addColorStop(1, "rgba(0, 65, 110, 0.149)");
        roundedRectPath(context, x, y, width, height, radius);
        context.fillStyle = fill;
        context.fill();

        function stroke(color, lineWidth, dash, dashOffset) {
            roundedRectPath(context, x, y, width, height, radius);
            context.strokeStyle = color;
            context.lineWidth = lineWidth;
            context.lineJoin = "miter";
            context.setLineDash(dash || []);
            context.lineDashOffset = dashOffset || 0;
            context.stroke();
        }
        stroke("rgba(0, 149, 230, 0.204)", 18);
        stroke("rgba(23, 190, 255, 0.439)", 9);
        stroke("rgba(10, 15, 18, 0.804)", 5);

        const steel = context.createLinearGradient(x, y, x, y + height);
        steel.addColorStop(0, "rgba(245, 250, 252, 0.961)");
        steel.addColorStop(0.24, "rgba(129, 147, 153, 0.941)");
        steel.addColorStop(0.52, "rgba(48, 61, 66, 0.961)");
        steel.addColorStop(0.78, "rgba(177, 190, 194, 0.941)");
        steel.addColorStop(1, "rgba(31, 42, 46, 0.961)");
        roundedRectPath(context, x, y, width, height, radius);
        context.strokeStyle = steel;
        context.lineWidth = 3;
        context.stroke();

        if (width > 3 && height > 3) {
            roundedRectPath(context, x + 1.5, y + 1.5, width - 3, height - 3, Math.max(0, radius - 1));
            context.strokeStyle = "rgba(86, 215, 255, 0.706)";
            context.lineWidth = 1;
            context.stroke();
        }
        if (width > 6 && height > 6) {
            context.setLineDash([4, 2]);
            context.lineDashOffset = 1.5;
            roundedRectPath(context, x + 3, y + 3, width - 6, height - 6, Math.max(0, radius - 2));
            context.strokeStyle = "rgba(36, 196, 255, 0.376)";
            context.lineWidth = 5;
            context.stroke();
            roundedRectPath(context, x + 3, y + 3, width - 6, height - 6, Math.max(0, radius - 2));
            context.strokeStyle = "rgba(102, 224, 255, 0.961)";
            context.lineWidth = 1.5;
            context.stroke();
        }
        context.setLineDash([]);
        context.lineDashOffset = 0;
    }

    function renderUnderlay() {
        state.underlayRenderPending = false;
        const mapView = state.lastMapView;
        if (!state.underlayCanvas || !shouldDisplay(mapView))
            return;
        const context = viewportContext(state.underlayCanvas, mapView.width, mapView.height);
        drawTileSelection(context, mapView);
        drawRectangleSelection(context);
    }

    function scheduleUnderlayRender() {
        if (state.underlayRenderPending)
            return;
        state.underlayRenderPending = true;
        window.requestAnimationFrame(renderUnderlay);
    }

    function drawSelectedNetwork(context, mapView) {
        if (!state.geometryReady)
            return;
        const selectedMarkers = selectedSet(state.visualState.selectedMarkerUuids);
        const selectedPipes = selectedSet(state.visualState.selectedPipeUuids);
        const entityWidth = Math.max(1, Number(state.visualState.entityWidth) || 10);

        for (const link of state.links) {
            if (link.entityType !== ENTITY_PIPE || !selectedPipes.has(link.uuid))
                continue;
            strokePolyline(context, link.vertices, (point) => screenPoint(point, mapView), SELECTED_COLOR, 3);
            context.fillStyle = SELECTED_COLOR;
            for (let index = 1; index + 1 < link.vertices.length; ++index) {
                const point = screenPoint(link.vertices[index], mapView);
                context.beginPath();
                context.arc(point.x, point.y, PIPE_VERTEX_RADIUS, 0, Math.PI * 2);
                context.fill();
            }
        }

        for (const link of state.links) {
            if (!isDeviceLink(link.entityType) || !selectedMarkers.has(link.uuid) || link.vertices.length < 2)
                continue;
            const center = linkCenter(link);
            if (!center)
                continue;
            const start = screenPoint(link.vertices[0], mapView);
            const middle = screenPoint(center, mapView);
            const end = screenPoint(link.vertices[link.vertices.length - 1], mapView);
            context.beginPath();
            context.moveTo(start.x, start.y);
            context.lineTo(middle.x, middle.y);
            context.lineTo(end.x, end.y);
            context.strokeStyle = SELECTED_COLOR;
            context.lineWidth = 3;
            context.lineCap = "round";
            context.lineJoin = "round";
            context.stroke();
            drawIcon(context, link.entityType, entityWidth, middle.x, middle.y, true, true);
        }

        for (const node of state.nodes) {
            if (!selectedMarkers.has(node.uuid))
                continue;
            const point = screenPoint(node, mapView);
            drawIcon(context, node.entityType, entityWidth, point.x, point.y, false, true);
        }
    }

    function drawPlacement(context, mapView) {
        const placement = state.visualState.placement || {};
        if (!placement.creating)
            return;

        const targetUuid = normalizeUuid(placement.connectionTargetUuid);
        const targetNode = state.nodesByUuid.get(targetUuid);
        if (targetNode) {
            const point = screenPoint(targetNode, mapView);
            context.beginPath();
            context.arc(point.x, point.y, CONNECTION_TARGET_RADIUS, 0, Math.PI * 2);
            context.fillStyle = PREVIEW_COLOR;
            context.fill();
        }

        if (Number(placement.entity) === ENTITY_PIPE && placement.pipeStartNodeUuid) {
            const startNode = state.nodesByUuid.get(normalizeUuid(placement.pipeStartNodeUuid));
            if (startNode) {
                const vertices = [startNode];
                if (Array.isArray(placement.pipeIntermediateVertices)) {
                    for (const coordinate of placement.pipeIntermediateVertices) {
                        if (!Array.isArray(coordinate) || coordinate.length < 2)
                            continue;
                        vertices.push(worldPoint(Number(coordinate[0]), Number(coordinate[1]), state.visualState.wrapReferenceLongitude));
                    }
                }
                let endPoint = { x: Number(placement.mouseX) || 0, y: Number(placement.mouseY) || 0 };
                if (targetNode)
                    endPoint = screenPoint(targetNode, mapView);
                const screenVertices = vertices.map((point) => screenPoint(point, mapView));
                screenVertices.push(endPoint);
                strokePolyline(context, screenVertices, (point) => point, PREVIEW_COLOR, 3);
            }
        }

        if (isDeviceLink(Number(placement.entity)) && placement.deviceLinkStartNodeUuid) {
            const startNode = state.nodesByUuid.get(normalizeUuid(placement.deviceLinkStartNodeUuid));
            if (startNode) {
                const start = screenPoint(startNode, mapView);
                const end = targetNode ? screenPoint(targetNode, mapView) : {
                    x: Number(placement.mouseX) || 0,
                    y: Number(placement.mouseY) || 0
                };
                const middle = { x: (start.x + end.x) / 2, y: (start.y + end.y) / 2 };
                context.beginPath();
                context.moveTo(start.x, start.y);
                context.lineTo(middle.x, middle.y);
                context.lineTo(end.x, end.y);
                context.strokeStyle = PREVIEW_COLOR;
                context.lineWidth = 3;
                context.lineCap = "round";
                context.lineJoin = "round";
                context.stroke();
            }
        }

        if (!placement.floatingMarkerVisible || Number(placement.entity) === 0 || Number(placement.floatingWidth) <= 0)
            return;

        const entityType = Number(placement.entity) | 0;
        const width = Number(placement.floatingWidth);
        if (isDeviceLink(entityType) && placement.deviceLinkStartNodeUuid) {
            const startNode = state.nodesByUuid.get(normalizeUuid(placement.deviceLinkStartNodeUuid));
            if (startNode) {
                const start = screenPoint(startNode, mapView);
                const end = targetNode ? screenPoint(targetNode, mapView) : {
                    x: Number(placement.mouseX) || 0,
                    y: Number(placement.mouseY) || 0
                };
                drawIcon(context, entityType, width, (start.x + end.x) / 2, (start.y + end.y) / 2, true, false);
                return;
            }
        }
        drawIcon(context, entityType, width, Number(placement.mouseX) || 0, Number(placement.mouseY) || 0, false, false);
    }

    function renderDynamic() {
        state.dynamicRenderPending = false;
        const mapView = state.lastMapView;
        if (!state.dynamicCanvas || !shouldDisplay(mapView))
            return;
        const context = viewportContext(state.dynamicCanvas, mapView.width, mapView.height);
        drawSelectedNetwork(context, mapView);
        drawPlacement(context, mapView);
    }

    function scheduleDynamicRender() {
        if (state.dynamicRenderPending)
            return;
        state.dynamicRenderPending = true;
        window.requestAnimationFrame(renderDynamic);
    }

    function updateViewport(mapView) {
        const underlayChanged = resizeViewportCanvas(state.underlayCanvas, mapView.width, mapView.height);
        const dynamicChanged = resizeViewportCanvas(state.dynamicCanvas, mapView.width, mapView.height);
        if (underlayChanged)
            scheduleUnderlayRender();
        if (dynamicChanged)
            scheduleDynamicRender();
    }

    function handleMapViewChanged(mapView) {
        state.lastMapView = mapView;
        if (!mapView || !ensureLayer(mapView.layer)) {
            if (state.layer)
                state.layer.style.display = "none";
            return;
        }

        const display = shouldDisplay(mapView);
        state.layer.style.display = display ? "block" : "none";
        if (!display)
            return;

        updateViewport(mapView);
        if (!state.geometryReady) {
            state.staticCanvas.style.display = "none";
        } else if (state.staticZoom !== mapView.zoom ||
                   state.staticEntityWidth !== Math.max(1, Number(state.visualState.entityWidth) || 10) ||
                   state.staticViewportFallback) {
            scheduleStaticRender();
        } else {
            positionStaticCanvas(mapView);
        }
        scheduleUnderlayRender();
        scheduleDynamicRender();
    }

    function setNetworkSnapshot(snapshot) {
        if (!snapshot || !Array.isArray(snapshot.nodes) || !Array.isArray(snapshot.links))
            throw new TypeError("Invalid AOWIS map editor network snapshot");
        state.networkSnapshot = snapshot;
        parseNetworkGeometry();
        invalidateStaticCache();
        scheduleDynamicRender();
    }

    function replaceSnapshotItems(targetItems, updates, collectionName) {
        const indicesByUuid = new Map();
        for (let index = 0; index < targetItems.length; ++index) {
            const item = targetItems[index];
            if (Array.isArray(item) && item.length > 2)
                indicesByUuid.set(normalizeUuid(item[2]), index);
        }

        for (const update of updates) {
            if (!Array.isArray(update) || update.length <= 2)
                continue;
            const uuid = normalizeUuid(update[2]);
            const index = indicesByUuid.get(uuid);
            if (index === undefined)
                throw new Error(`Unknown ${collectionName} UUID in geometry patch: ${uuid}`);
            targetItems[index] = update;
        }
    }

    function updateGeometry(patch) {
        if (!state.networkSnapshot || !patch || !Array.isArray(patch.nodes) || !Array.isArray(patch.links))
            throw new TypeError("Invalid AOWIS map editor geometry patch");

        replaceSnapshotItems(state.networkSnapshot.nodes, patch.nodes, "node");
        replaceSnapshotItems(state.networkSnapshot.links, patch.links, "link");
        state.networkSnapshot.geometryRevision = patch.geometryRevision;
        state.networkSnapshot.visualRevision = patch.visualRevision;
        parseNetworkGeometry();
        invalidateStaticCache();
        scheduleDynamicRender();
    }

    function setVisualState(visualState) {
        if (!visualState || typeof visualState !== "object")
            throw new TypeError("Invalid AOWIS map editor visual state");
        const previousWrapReference = Number(state.visualState.wrapReferenceLongitude) || 0;
        const previousEntityWidth = Number(state.visualState.entityWidth) || 10;
        state.visualState = visualState;
        const wrapReference = Number(visualState.wrapReferenceLongitude) || 0;
        const entityWidth = Number(visualState.entityWidth) || 10;
        if (wrapReference !== previousWrapReference || wrapReference !== state.geometryWrapReferenceLongitude) {
            parseNetworkGeometry();
            invalidateStaticCache();
        } else if (entityWidth !== previousEntityWidth) {
            invalidateStaticCache();
        }
        scheduleDynamicRender();
    }

    function setViewportState(viewportState) {
        if (!viewportState || typeof viewportState !== "object")
            throw new TypeError("Invalid AOWIS map editor viewport state");
        state.viewportState = viewportState;
        applyBackground();
        scheduleUnderlayRender();
    }

    function setBackground(red, green, blue) {
        state.backgroundRed = Math.max(0, Math.min(255, Number(red) || 0));
        state.backgroundGreen = Math.max(0, Math.min(255, Number(green) || 0));
        state.backgroundBlue = Math.max(0, Math.min(255, Number(blue) || 0));
        applyBackground();
    }

    function clearCanvas(canvas) {
        if (!canvas)
            return;
        const context = canvas.getContext("2d");
        context.setTransform(1, 0, 0, 1, 0, 0);
        context.clearRect(0, 0, canvas.width, canvas.height);
    }

    function clear() {
        state.networkSnapshot = null;
        state.visualState = defaultVisualState();
        state.viewportState = defaultViewportState();
        state.nodes = [];
        state.links = [];
        state.nodesByUuid.clear();
        state.geometryReady = false;
        state.geometryWrapReferenceLongitude = Number.NaN;
        state.staticZoom = null;
        state.staticEntityWidth = 0;
        state.staticViewportFallback = false;
        clearCanvas(state.underlayCanvas);
        clearCanvas(state.staticCanvas);
        clearCanvas(state.dynamicCanvas);
        if (state.staticCanvas)
            state.staticCanvas.style.display = "none";
        applyBackground();
    }

    function destroy() {
        if (state.unsubscribeView)
            state.unsubscribeView();
        state.unsubscribeView = null;
        if (state.layer)
            state.layer.remove();
        state.layer = null;
        state.underlayCanvas = null;
        state.staticCanvas = null;
        state.dynamicCanvas = null;
        state.icons.clear();
        state.tintedIcons.clear();
        clear();
    }

    function initialize() {
        if (!window.aowisBrowserMap || typeof window.aowisBrowserMap.subscribeView !== "function")
            throw new Error("AOWIS browser map editor requires aowis-browser-map.js");
        state.unsubscribeView = window.aowisBrowserMap.subscribeView(handleMapViewChanged);
    }

    window.aowisBrowserMapEditor = {
        setNetworkSnapshot: setNetworkSnapshot,
        updateGeometry: updateGeometry,
        setVisualState: setVisualState,
        setViewportState: setViewportState,
        setBackground: setBackground,
        clear: clear,
        destroy: destroy
    };

    initialize();
})();
