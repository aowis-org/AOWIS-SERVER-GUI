(function () {
    "use strict";

    const SHARED_RENDERER = window.aowisBrowserVector;
    if (!SHARED_RENDERER)
        throw new Error("AOWIS browser map editor requires aowis-browser-vector.js");

    const REFERENCE_ZOOM = SHARED_RENDERER.REFERENCE_ZOOM;
    const STATIC_PADDING = 64;
    const MAX_STATIC_DIMENSION = 8192;
    const MAX_STATIC_AREA = 16 * 1024 * 1024;
    const STATIC_RASTER_MAX_AREA = 8 * 1024 * 1024;
    const STATIC_MAX_PIXEL_RATIO = 1.5;
    const STATIC_ZOOM_SETTLE_DELAY_MS = 90;
    const MARKER_DOT_RADIUS = 5;
    const CONNECTION_TARGET_RADIUS = 9;
    const PIPE_VERTEX_RADIUS = 4;
    const ENTITY_PIPE = SHARED_RENDERER.ENTITY_PIPE;
    const ENTITY_PUMP = SHARED_RENDERER.ENTITY_PUMP;
    const ENTITY_VALVE = SHARED_RENDERER.ENTITY_VALVE;
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
        staticRetained: null,
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
        staticRebuildTimer: 0,
        staticRebuildNotBefore: 0,
        underlayRenderPending: false,
        dynamicRenderPending: false,
        icons: new Map(),
        tintedIcons: new Map(),
        backgroundRed: 255,
        backgroundGreen: 255,
        backgroundBlue: 255,
        ownerId: 0
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
            },
            move: {
                active: false,
                sessionId: "0",
                markers: [],
                links: []
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

    function vectorRenderer() {
        return SHARED_RENDERER;
    }

    function devicePixelRatio() {
        return projection().devicePixelRatio();
    }

    function createCanvas(zIndex) {
        return vectorRenderer().createCanvas(zIndex);
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
            state.staticRetained = new (vectorRenderer().RetainedCanvasLayer)(2);
            state.staticCanvas = state.staticRetained.canvas;
            state.staticCanvas.style.willChange = "transform";
            state.dynamicCanvas = createCanvas(3);
            state.layer.appendChild(state.underlayCanvas);
            state.staticRetained.attach(state.layer);
            state.layer.appendChild(state.dynamicCanvas);
            applyBackground();
        }

        if (state.layer.parentElement !== mapLayer)
            mapLayer.appendChild(state.layer);
        return true;
    }

    function shouldDisplay(mapView) {
        return Boolean(mapView && state.ownerId !== 0 &&
            mapView.activeOwner === state.ownerId && mapView.topmost &&
            mapView.visible && mapView.ready && mapView.initialized &&
            mapView.width > 0 && mapView.height > 0);
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
        return vectorRenderer().normalizeUuid(value);
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

        const mapProjection = projection();
        const wrapReference = Number(state.visualState.wrapReferenceLongitude) || 0;
        const referenceX = mapProjection.longitudeToWorldPixel(wrapReference, REFERENCE_ZOOM);
        const geometry = vectorRenderer().projectNetworkSnapshot(snapshot, {
            anchorX: referenceX,
            longitudeToWorldPixel: (longitude) =>
                mapProjection.longitudeToWorldPixel(longitude, REFERENCE_ZOOM),
            latitudeToWorldPixel: (latitude) =>
                mapProjection.latitudeToWorldPixel(latitude, REFERENCE_ZOOM),
            nearestWrappedWorldPixel: (rawX, reference) =>
                mapProjection.nearestWrappedWorldPixel(rawX, reference, REFERENCE_ZOOM)
        });
        if (!geometry)
            return;

        state.nodes = geometry.nodes;
        state.links = geometry.links;
        state.nodesByUuid = geometry.nodesByUuid;
        state.geometryWrapReferenceLongitude = wrapReference;
        state.geometryMinimumX = geometry.minimumX;
        state.geometryMinimumY = geometry.minimumY;
        state.geometryMaximumX = geometry.maximumX;
        state.geometryMaximumY = geometry.maximumY;
        state.geometryReady = geometry.ready;
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
        return vectorRenderer().polylineMidpoint(link.vertices);
    }

    function isDeviceLink(entityType) {
        return entityType === ENTITY_PUMP || entityType === ENTITY_VALVE;
    }

    function staticRenderPixelRatio(width, height) {
        const cssWidth = Math.max(1, Number(width) || 1);
        const cssHeight = Math.max(1, Number(height) || 1);
        return Math.max(0.5, Math.min(
            devicePixelRatio(),
            STATIC_MAX_PIXEL_RATIO,
            MAX_STATIC_DIMENSION / cssWidth,
            MAX_STATIC_DIMENSION / cssHeight,
            Math.sqrt(STATIC_RASTER_MAX_AREA / (cssWidth * cssHeight))));
    }

    function clearScheduledStaticRender() {
        if (state.staticRebuildTimer !== 0) {
            window.clearTimeout(state.staticRebuildTimer);
            state.staticRebuildTimer = 0;
        }
        if (state.staticRetained)
            state.staticRetained.cancelScheduled();
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
        const ratio = staticRenderPixelRatio(width, height);
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

    function applyStaticSpecification(specification) {
        state.staticCssWidth = specification.width;
        state.staticCssHeight = specification.height;
        state.staticOriginX = specification.originX;
        state.staticOriginY = specification.originY;
        state.staticViewportFallback = specification.fallback;
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

    function moveState() {
        const move = state.visualState && state.visualState.move;
        return move && typeof move === "object" ? move : { active: false, sessionId: "0", markers: [], links: [] };
    }

    function movingUuidSets() {
        const move = moveState();
        const markers = new Set();
        const links = new Set();
        if (!move.active)
            return { markers: markers, links: links };

        if (Array.isArray(move.markers)) {
            for (const marker of move.markers) {
                const uuid = normalizeUuid(marker && marker.uuid);
                if (uuid)
                    markers.add(uuid);
            }
        }
        if (Array.isArray(move.links)) {
            for (const link of move.links) {
                const uuid = normalizeUuid(link && link.uuid);
                if (uuid)
                    links.add(uuid);
            }
        }
        return { markers: markers, links: links };
    }

    function renderStaticNetwork() {
        const mapView = state.lastMapView;
        if (!state.staticCanvas || !state.geometryReady || !shouldDisplay(mapView)) {
            if (state.staticCanvas)
                state.staticCanvas.style.display = "none";
            return;
        }

        const specification = staticSpecification(mapView);
        applyStaticSpecification(specification);
        const renderer = vectorRenderer();
        const documentVector = new renderer.VectorDocument();
        const pointFunction = (point) => cachedPoint(point, specification, mapView);
        const entityWidth = Math.max(1, Number(state.visualState.entityWidth) || 10);
        const moving = movingUuidSets();

        const pipePath = renderer.createPath();
        const pipeVertexPath = renderer.createPath();
        for (const link of state.links) {
            if (link.entityType !== ENTITY_PIPE || moving.links.has(link.uuid))
                continue;
            renderer.addPolyline(pipePath, link.vertices, pointFunction);
            for (let index = 1; index + 1 < link.vertices.length; ++index) {
                const point = pointFunction(link.vertices[index]);
                renderer.addCircle(pipeVertexPath, point.x, point.y, PIPE_VERTEX_RADIUS);
            }
        }
        documentVector.addStroke(pipePath, "black", 3);
        documentVector.addFill(pipeVertexPath, "black");

        const devicePath = renderer.createPath();
        for (const link of state.links) {
            if (!isDeviceLink(link.entityType) || moving.links.has(link.uuid) || link.vertices.length < 2)
                continue;
            const center = linkCenter(link);
            if (!center)
                continue;
            const start = pointFunction(link.vertices[0]);
            const middle = pointFunction(center);
            const end = pointFunction(link.vertices[link.vertices.length - 1]);
            devicePath.moveTo(start.x, start.y);
            devicePath.lineTo(middle.x, middle.y);
            devicePath.lineTo(end.x, end.y);
        }
        documentVector.addStroke(devicePath, DEVICE_LINK_COLOR, 3);

        const nodeDotPath = renderer.createPath();
        for (const node of state.nodes) {
            if (moving.markers.has(node.uuid))
                continue;
            const point = pointFunction(node);
            renderer.addCircle(nodeDotPath, point.x, point.y, MARKER_DOT_RADIUS);
        }
        documentVector.addFill(nodeDotPath, "black");

        for (const link of state.links) {
            if (!isDeviceLink(link.entityType) || moving.links.has(link.uuid) || link.vertices.length < 2)
                continue;
            const center = linkCenter(link);
            if (!center)
                continue;
            const point = pointFunction(center);
            const dimensions = iconDimensions(link.entityType, entityWidth);
            if (!dimensions)
                continue;
            documentVector.addImage(
                dimensions.image,
                point.x - dimensions.width / 2,
                point.y - dimensions.height / 2,
                dimensions.width,
                dimensions.height);
        }

        for (const node of state.nodes) {
            if (moving.markers.has(node.uuid))
                continue;
            const point = pointFunction(node);
            const dimensions = iconDimensions(node.entityType, entityWidth);
            if (!dimensions)
                continue;
            documentVector.addImage(
                dimensions.image,
                Math.round(point.x),
                Math.round(point.y) - dimensions.height,
                dimensions.width,
                dimensions.height);
        }

        if (!state.staticRetained || !state.staticRetained.render(
            documentVector,
            specification.width,
            specification.height,
            staticRenderPixelRatio(specification.width, specification.height))) {
            return;
        }
        state.staticZoom = mapView.zoom;
        state.staticEntityWidth = entityWidth;
        state.staticRebuildNotBefore = 0;
        state.staticCanvas.style.display = "block";
        positionStaticCanvas(mapView);
    }
    function positionStaticCanvas(mapView) {
        if (!state.staticCanvas || state.staticZoom === null)
            return;
        if (state.staticViewportFallback) {
            if (state.staticZoom === mapView.zoom) {
                state.staticCanvas.style.display = "block";
                state.staticCanvas.style.transform = "translate3d(0, 0, 0)";
            } else {
                state.staticCanvas.style.display = "none";
            }
            return;
        }

        const scale = Math.pow(2, mapView.zoom - REFERENCE_ZOOM);
        const renderedScale = Math.pow(2, state.staticZoom - REFERENCE_ZOOM);
        const zoomScale = scale / renderedScale;
        const originAtZoom = worldXAtMapZoom(state.staticOriginX, mapView);
        const left = projection().snapToPhysicalPixel(
            mapView.width / 2 + originAtZoom - mapView.centerPixelX);
        const top = projection().snapToPhysicalPixel(
            mapView.height / 2 + state.staticOriginY * scale - mapView.centerPixelY);
        state.staticCanvas.style.display = "block";
        state.staticCanvas.style.transform =
            `translate3d(${left}px, ${top}px, 0) scale(${zoomScale})`;
    }

    function scheduleStaticRender() {
        if (!state.staticRetained)
            return;

        const delay = state.staticRebuildNotBefore - performance.now();
        if (delay > 1) {
            if (state.staticRebuildTimer === 0) {
                state.staticRebuildTimer = window.setTimeout(() => {
                    state.staticRebuildTimer = 0;
                    scheduleStaticRender();
                }, Math.ceil(delay));
            }
            return;
        }

        if (state.staticRebuildTimer !== 0) {
            window.clearTimeout(state.staticRebuildTimer);
            state.staticRebuildTimer = 0;
        }
        state.staticRetained.schedule(renderStaticNetwork);
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
        const moving = movingUuidSets();
        const entityWidth = Math.max(1, Number(state.visualState.entityWidth) || 10);

        for (const link of state.links) {
            if (link.entityType !== ENTITY_PIPE || moving.links.has(link.uuid) || !selectedPipes.has(link.uuid))
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
            if (!isDeviceLink(link.entityType) || moving.links.has(link.uuid) || !selectedMarkers.has(link.uuid) || link.vertices.length < 2)
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
            if (moving.markers.has(node.uuid) || !selectedMarkers.has(node.uuid))
                continue;
            const point = screenPoint(node, mapView);
            drawIcon(context, node.entityType, entityWidth, point.x, point.y, false, true);
        }
    }

    function drawMove(context, mapView) {
        const move = moveState();
        if (!move.active)
            return;

        const entityWidth = Math.max(1, Number(state.visualState.entityWidth) || 10);
        if (Array.isArray(move.links)) {
            for (const link of move.links) {
                if (!link || !Array.isArray(link.vertices) || link.vertices.length < 2)
                    continue;

                const entityType = Number(link.entity) | 0;
                const vertices = [];
                for (const coordinate of link.vertices) {
                    const point = screenFromCoordinate(coordinate, mapView);
                    if (point)
                        vertices.push(point);
                }
                if (vertices.length < 2)
                    continue;

                strokePolyline(context, vertices, (point) => point, SELECTED_COLOR, 3);
                if (entityType === ENTITY_PIPE) {
                    context.fillStyle = SELECTED_COLOR;
                    for (let index = 1; index + 1 < vertices.length; ++index) {
                        context.beginPath();
                        context.arc(vertices[index].x, vertices[index].y, PIPE_VERTEX_RADIUS, 0, Math.PI * 2);
                        context.fill();
                    }
                } else if (isDeviceLink(entityType)) {
                    const center = vectorRenderer().polylineMidpoint(vertices);
                    if (center)
                        drawIcon(context, entityType, entityWidth, center.x, center.y, true, true);
                }
            }
        }

        if (!Array.isArray(move.markers))
            return;
        for (const marker of move.markers) {
            if (!marker)
                continue;
            const entityType = Number(marker.entity) | 0;
            if (isDeviceLink(entityType))
                continue;
            const point = screenFromCoordinate(marker.coordinate, mapView);
            if (!point)
                continue;
            drawIcon(context, entityType, entityWidth, point.x, point.y, false, true);
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
        drawMove(context, mapView);
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
        const previousMapView = state.lastMapView;
        if (previousMapView && mapView && previousMapView.zoom !== mapView.zoom)
            state.staticRebuildNotBefore = performance.now() + STATIC_ZOOM_SETTLE_DELAY_MS;
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
            positionStaticCanvas(mapView);
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
        const previousMove = moveState();
        const previousMoveActive = Boolean(previousMove.active);
        const previousMoveSessionId = String(previousMove.sessionId || "0");
        state.visualState = visualState;
        const wrapReference = Number(visualState.wrapReferenceLongitude) || 0;
        const entityWidth = Number(visualState.entityWidth) || 10;
        const nextMove = moveState();
        const nextMoveActive = Boolean(nextMove.active);
        const nextMoveSessionId = String(nextMove.sessionId || "0");
        if (wrapReference !== previousWrapReference || wrapReference !== state.geometryWrapReferenceLongitude) {
            parseNetworkGeometry();
            invalidateStaticCache();
        } else if (entityWidth !== previousEntityWidth ||
                   previousMoveActive !== nextMoveActive ||
                   (nextMoveActive && previousMoveSessionId !== nextMoveSessionId)) {
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

    function setOwnerId(ownerId) {
        state.ownerId = Number(ownerId) | 0;
        if (state.lastMapView)
            handleMapViewChanged(state.lastMapView);
    }

    function clearCanvas(canvas) {
        if (!canvas)
            return;
        const context = canvas.getContext("2d");
        context.setTransform(1, 0, 0, 1, 0, 0);
        context.clearRect(0, 0, canvas.width, canvas.height);
    }

    function clear() {
        clearScheduledStaticRender();
        state.staticRebuildNotBefore = 0;
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
        if (state.staticRetained)
            state.staticRetained.clear();
        else
            clearCanvas(state.staticCanvas);
        clearCanvas(state.dynamicCanvas);
        applyBackground();
    }

    function destroy() {
        if (state.unsubscribeView)
            state.unsubscribeView();
        state.unsubscribeView = null;
        clearScheduledStaticRender();
        if (state.staticRetained)
            state.staticRetained.destroy();
        state.staticRetained = null;
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
        vectorRenderer();
        state.unsubscribeView = window.aowisBrowserMap.subscribeView(handleMapViewChanged);
    }

    window.aowisBrowserMapEditor = {
        setNetworkSnapshot: setNetworkSnapshot,
        updateGeometry: updateGeometry,
        setVisualState: setVisualState,
        setViewportState: setViewportState,
        setBackground: setBackground,
        setOwnerId: setOwnerId,
        clear: clear,
        destroy: destroy
    };

    initialize();
})();
