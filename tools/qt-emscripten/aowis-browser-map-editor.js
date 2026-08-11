(function () {
    "use strict";

    const SHARED_RENDERER = window.aowisBrowserVector;
    if (!SHARED_RENDERER)
        throw new Error("AOWIS browser map editor requires aowis-browser-vector.js");
    const WEBGL_RENDERER = window.aowisBrowserNetworkWebGl;
    if (!WEBGL_RENDERER)
        throw new Error("AOWIS browser map editor requires aowis-browser-network-webgl.js");

    const REFERENCE_ZOOM = SHARED_RENDERER.REFERENCE_ZOOM;
    const NETWORK_MAX_PIXEL_RATIO = 1.5;
    const MARKER_DOT_RADIUS = 5;
    const CONNECTION_TARGET_RADIUS = 9;
    const PIPE_VERTEX_RADIUS = 4;
    const ENTITY_PIPE = SHARED_RENDERER.ENTITY_PIPE;
    const ENTITY_PUMP = SHARED_RENDERER.ENTITY_PUMP;
    const ENTITY_VALVE = SHARED_RENDERER.ENTITY_VALVE;
    const BLACK = Object.freeze([0, 0, 0, 1]);
    const WHITE = Object.freeze([1, 1, 1, 1]);
    const SELECTED_COLOR = Object.freeze([0, 190 / 255, 1, 1]);
    const SELECTED_GLOW_COLOR = Object.freeze([0, 190 / 255, 1, 0.8]);
    const PREVIEW_COLOR = Object.freeze([0, 140 / 255, 1, 1]);
    const DEVICE_LINK_COLOR = Object.freeze([139 / 255, 90 / 255, 43 / 255, 1]);

    const GLOW_OFFSETS = Object.freeze([
        [-6, 0], [6, 0], [0, -6], [0, 6], [-5, -3], [-5, 3], [5, -3], [5, 3],
        [-3, -5], [-3, 5], [3, -5], [3, 5], [-4, 0], [4, 0], [0, -4], [0, 4],
        [-3, -3], [-3, 3], [3, -3], [3, 3], [-2, 0], [2, 0], [0, -2], [0, 2],
        [-2, -2], [-2, 2], [2, -2], [2, 2]
    ]);

    const ICON_PATHS = new Map([
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
    const ICON_SOURCES = [];
    const ICON_SOURCE_BY_PATH = new Map();
    for (const path of ICON_PATHS.values()) {
        if (ICON_SOURCE_BY_PATH.has(path))
            continue;
        const source = Object.freeze({ path: path, slot: ICON_SOURCES.length });
        ICON_SOURCES.push(source);
        ICON_SOURCE_BY_PATH.set(path, source);
    }

    const state = {
        layer: null,
        underlayCanvas: null,
        rectangleSelectionElement: null,
        networkCanvas: null,
        networkRenderer: null,
        networkWebGlUnavailable: false,
        networkRenderFrameRequest: 0,
        underlayRenderPending: false,
        baseDirty: true,
        overlayDirty: true,
        spriteAtlasDirty: true,
        networkSnapshot: null,
        visualState: defaultVisualState(),
        viewportState: defaultViewportState(),
        lastMapView: null,
        unsubscribeView: null,
        nodes: [],
        links: [],
        nodesByUuid: new Map(),
        geometryReady: false,
        geometryOriginX: 0,
        geometryOriginY: 0,
        geometryWrapReferenceLongitude: Number.NaN,
        iconImages: new Map(),
        iconLoadGeneration: 0,
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

    function devicePixelRatio() {
        return projection().devicePixelRatio();
    }

    function createCanvas(zIndex) {
        return SHARED_RENDERER.createCanvas(zIndex);
    }

    function applyBackground() {
        if (!state.layer)
            return;
        const opacity = Math.max(
            0, Math.min(100, Number(state.viewportState.backgroundOpacity) || 0));
        state.layer.style.backgroundColor =
            `rgba(${state.backgroundRed}, ${state.backgroundGreen}, ${state.backgroundBlue}, ${opacity / 100})`;
    }

    function disableNetworkRenderer(error) {
        if (!state.networkWebGlUnavailable && error)
            console.error("AOWIS map editor WebGL2 renderer failed:", error);
        state.networkWebGlUnavailable = true;
        if (state.networkRenderer)
            state.networkRenderer.destroy();
        state.networkRenderer = null;
        if (state.networkCanvas)
            state.networkCanvas.remove();
        state.networkCanvas = null;
    }

    function createNetworkRenderer() {
        const canvas = createCanvas(2);
        canvas.style.display = "none";
        canvas.style.background = "transparent";
        canvas.style.willChange = "contents";
        state.layer.appendChild(canvas);
        const renderer = WEBGL_RENDERER.create(canvas, {
            contextRestored: scheduleNetworkRender,
            spritesReady: scheduleNetworkRender,
            error: disableNetworkRenderer
        });
        if (!renderer) {
            canvas.remove();
            state.networkWebGlUnavailable = true;
            return;
        }
        state.networkCanvas = canvas;
        state.networkRenderer = renderer;
        state.baseDirty = true;
        state.overlayDirty = true;
        state.spriteAtlasDirty = true;
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
            state.layer.appendChild(state.underlayCanvas);
            if (!state.networkWebGlUnavailable)
                createNetworkRenderer();

            state.rectangleSelectionElement = document.createElement("div");
            state.rectangleSelectionElement.style.position = "absolute";
            state.rectangleSelectionElement.style.left = "0";
            state.rectangleSelectionElement.style.top = "0";
            state.rectangleSelectionElement.style.display = "none";
            state.rectangleSelectionElement.style.pointerEvents = "none";
            state.rectangleSelectionElement.style.boxSizing = "border-box";
            state.rectangleSelectionElement.style.border =
                "3px solid rgba(190, 235, 250, 0.96)";
            state.rectangleSelectionElement.style.borderRadius = "2px";
            state.rectangleSelectionElement.style.background =
                "linear-gradient(rgba(35, 151, 211, 0.10), rgba(0, 65, 110, 0.15))";
            state.rectangleSelectionElement.style.boxShadow =
                "0 0 0 2px rgba(10, 15, 18, 0.80), "
                + "0 0 0 6px rgba(23, 190, 255, 0.44), "
                + "inset 0 0 0 1px rgba(86, 215, 255, 0.71)";
            state.rectangleSelectionElement.style.willChange = "transform, width, height";
            state.rectangleSelectionElement.style.zIndex = "3";
            state.layer.appendChild(state.rectangleSelectionElement);
            applyBackground();
        }

        if (state.layer.parentElement !== mapLayer)
            mapLayer.appendChild(state.layer);
        return true;
    }

    function shouldDisplay(mapView) {
        return Boolean(mapView && state.ownerId !== 0
            && mapView.activeOwner === state.ownerId && mapView.topmost
            && mapView.visible && mapView.ready && mapView.initialized
            && mapView.width > 0 && mapView.height > 0);
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

    function normalizeUuid(value) {
        return SHARED_RENDERER.normalizeUuid(value);
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
            x: mapProjection.nearestWrappedWorldPixel(
                rawX, referenceX, REFERENCE_ZOOM),
            y: mapProjection.latitudeToWorldPixel(latitude, REFERENCE_ZOOM)
        };
    }

    function worldPointFromCoordinate(coordinate) {
        if (!Array.isArray(coordinate) || coordinate.length < 2)
            return null;
        return worldPoint(
            Number(coordinate[0]),
            Number(coordinate[1]),
            state.visualState.wrapReferenceLongitude);
    }

    function parseNetworkGeometry() {
        state.nodes = [];
        state.links = [];
        state.nodesByUuid.clear();
        state.geometryReady = false;
        state.geometryOriginX = 0;
        state.geometryOriginY = 0;

        const snapshot = state.networkSnapshot;
        if (!snapshot || !Array.isArray(snapshot.nodes) || !Array.isArray(snapshot.links))
            return;

        const mapProjection = projection();
        const wrapReference = Number(state.visualState.wrapReferenceLongitude) || 0;
        const referenceX = mapProjection.longitudeToWorldPixel(
            wrapReference, REFERENCE_ZOOM);
        const geometry = SHARED_RENDERER.projectNetworkSnapshot(snapshot, {
            anchorX: referenceX,
            longitudeToWorldPixel: (longitude) =>
                mapProjection.longitudeToWorldPixel(longitude, REFERENCE_ZOOM),
            latitudeToWorldPixel: (latitude) =>
                mapProjection.latitudeToWorldPixel(latitude, REFERENCE_ZOOM),
            nearestWrappedWorldPixel: (rawX, reference) =>
                mapProjection.nearestWrappedWorldPixel(
                    rawX, reference, REFERENCE_ZOOM)
        });
        if (!geometry)
            return;

        state.nodes = geometry.nodes;
        state.links = geometry.links;
        state.nodesByUuid = geometry.nodesByUuid;
        state.geometryOriginX = geometry.originX;
        state.geometryOriginY = geometry.originY;
        state.geometryWrapReferenceLongitude = wrapReference;
        state.geometryReady = geometry.ready;
    }

    function iconDimensions(entityType, width) {
        const path = ICON_PATHS.get(entityType) || "map-editor-icons/geomarker.png";
        const source = ICON_SOURCE_BY_PATH.get(path);
        const entry = state.iconImages.get(path);
        if (!source || !entry || !entry.loaded || entry.image.naturalWidth <= 0)
            return null;
        return {
            width: width,
            height: entry.image.naturalHeight * width / entry.image.naturalWidth,
            slot: source.slot
        };
    }

    function loadIcon(source) {
        return new Promise((resolve) => {
            const image = new Image();
            image.decoding = "async";
            image.onload = function () {
                resolve({
                    path: source.path,
                    slot: source.slot,
                    image: image,
                    loaded: true
                });
            };
            image.onerror = function () {
                console.error(`Could not load map editor icon: ${source.path}`);
                resolve({
                    path: source.path,
                    slot: source.slot,
                    image: image,
                    loaded: false
                });
            };
            image.src = source.path;
        });
    }

    async function loadIconImages() {
        const generation = ++state.iconLoadGeneration;
        const entries = await Promise.all(ICON_SOURCES.map(loadIcon));
        if (generation !== state.iconLoadGeneration)
            return;
        state.iconImages.clear();
        for (const entry of entries)
            state.iconImages.set(entry.path, entry);
        state.spriteAtlasDirty = true;
        state.baseDirty = true;
        state.overlayDirty = true;
        scheduleNetworkRender();
    }

    function linkCenter(vertices) {
        return SHARED_RENDERER.polylineMidpoint(vertices);
    }

    function isDeviceLink(entityType) {
        return entityType === ENTITY_PUMP || entityType === ENTITY_VALVE;
    }

    function moveState() {
        const move = state.visualState && state.visualState.move;
        return move && typeof move === "object"
            ? move : { active: false, sessionId: "0", markers: [], links: [] };
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

    function worldTransform(mapView) {
        const mapProjection = projection();
        const scale = Math.pow(2, mapView.zoom - REFERENCE_ZOOM);
        const worldSize = mapProjection.tileSize * Math.pow(2, mapView.zoom);
        let originPixelX = state.geometryOriginX * scale;
        originPixelX += Math.round(
            (mapView.centerPixelX - originPixelX) / worldSize) * worldSize;
        return {
            scale: scale,
            translateX: originPixelX
                - mapView.originTileX * mapProjection.tileSize + mapView.translateX,
            translateY: state.geometryOriginY * scale
                - mapView.originTileY * mapProjection.tileSize + mapView.translateY
        };
    }

    function worldPointFromScreen(x, y, mapView) {
        const transform = worldTransform(mapView);
        return {
            x: state.geometryOriginX + (x - transform.translateX) / transform.scale,
            y: state.geometryOriginY + (y - transform.translateY) / transform.scale
        };
    }

    function createBatchBuilder() {
        return {
            segments: [],
            segmentColors: [],
            discs: [],
            discColors: [],
            sprites: [],
            spriteColors: []
        };
    }

    function appendColor(target, color) {
        target.push(color[0], color[1], color[2], color[3]);
    }

    function appendSegment(builder, start, end, color) {
        if (!start || !end)
            return;
        builder.segments.push(
            start.x - state.geometryOriginX,
            start.y - state.geometryOriginY,
            end.x - state.geometryOriginX,
            end.y - state.geometryOriginY);
        appendColor(builder.segmentColors, color);
    }

    function appendPolyline(builder, vertices, color) {
        if (!Array.isArray(vertices))
            return;
        for (let index = 1; index < vertices.length; ++index)
            appendSegment(builder, vertices[index - 1], vertices[index], color);
    }

    function appendDeviceLink(builder, vertices, color) {
        if (!Array.isArray(vertices) || vertices.length < 2)
            return;
        const center = linkCenter(vertices);
        if (!center)
            return;
        appendSegment(builder, vertices[0], center, color);
        appendSegment(builder, center, vertices[vertices.length - 1], color);
    }

    function appendDisc(builder, point, radius, color) {
        if (!point || !(radius > 0))
            return;
        builder.discs.push(
            point.x - state.geometryOriginX,
            point.y - state.geometryOriginY,
            radius);
        appendColor(builder.discColors, color);
    }

    function appendSprite(
        builder, point, dimensions, anchorX, anchorY, offsetX, offsetY, tint, color) {
        if (!point || !dimensions)
            return;
        builder.sprites.push(
            point.x - state.geometryOriginX,
            point.y - state.geometryOriginY,
            dimensions.width,
            dimensions.height,
            anchorX,
            anchorY,
            offsetX,
            offsetY,
            dimensions.slot,
            tint ? 1 : 0);
        appendColor(builder.spriteColors, color);
    }

    function appendIcon(builder, entityType, width, point, centered, selected) {
        const dimensions = iconDimensions(entityType, width);
        if (!dimensions)
            return;
        const anchorX = centered ? 0.5 : 0;
        const anchorY = centered ? 0.5 : 1;
        if (selected) {
            for (const offset of GLOW_OFFSETS) {
                appendSprite(
                    builder, point, dimensions, anchorX, anchorY,
                    offset[0], offset[1], true, SELECTED_GLOW_COLOR);
            }
        }
        appendSprite(
            builder, point, dimensions, anchorX, anchorY,
            0, 0, false, WHITE);
    }

    function commitBatch(batchName, builder) {
        state.networkRenderer.setGeometry(batchName, {
            segments: new Float32Array(builder.segments),
            discs: new Float32Array(builder.discs),
            sprites: new Float32Array(builder.sprites)
        });
        state.networkRenderer.setColors(batchName, {
            segments: new Float32Array(builder.segmentColors),
            discs: new Float32Array(builder.discColors),
            sprites: new Float32Array(builder.spriteColors)
        });
    }

    function buildBaseBatch() {
        const builder = createBatchBuilder();
        if (!state.geometryReady) {
            commitBatch("base", builder);
            return;
        }

        const moving = movingUuidSets();
        const entityWidth = Math.max(
            1, Number(state.visualState.entityWidth) || 10);
        for (const link of state.links) {
            if (moving.links.has(link.uuid))
                continue;
            if (link.entityType === ENTITY_PIPE) {
                appendPolyline(builder, link.vertices, BLACK);
                for (let index = 1; index + 1 < link.vertices.length; ++index)
                    appendDisc(builder, link.vertices[index], PIPE_VERTEX_RADIUS, BLACK);
                continue;
            }
            if (!isDeviceLink(link.entityType))
                continue;
            appendDeviceLink(builder, link.vertices, DEVICE_LINK_COLOR);
            const center = linkCenter(link.vertices);
            if (center)
                appendIcon(builder, link.entityType, entityWidth, center, true, false);
        }

        for (const node of state.nodes) {
            if (moving.markers.has(node.uuid))
                continue;
            appendDisc(builder, node, MARKER_DOT_RADIUS, BLACK);
            appendIcon(builder, node.entityType, entityWidth, node, false, false);
        }
        commitBatch("base", builder);
    }

    function appendSelectedNetwork(builder) {
        const selectedMarkers = selectedSet(state.visualState.selectedMarkerUuids);
        const selectedPipes = selectedSet(state.visualState.selectedPipeUuids);
        const moving = movingUuidSets();
        const entityWidth = Math.max(
            1, Number(state.visualState.entityWidth) || 10);

        for (const link of state.links) {
            if (moving.links.has(link.uuid))
                continue;
            if (link.entityType === ENTITY_PIPE && selectedPipes.has(link.uuid)) {
                appendPolyline(builder, link.vertices, SELECTED_COLOR);
                for (let index = 1; index + 1 < link.vertices.length; ++index) {
                    appendDisc(
                        builder, link.vertices[index], PIPE_VERTEX_RADIUS, SELECTED_COLOR);
                }
            } else if (isDeviceLink(link.entityType)
                       && selectedMarkers.has(link.uuid)) {
                appendDeviceLink(builder, link.vertices, SELECTED_COLOR);
                const center = linkCenter(link.vertices);
                if (center)
                    appendIcon(builder, link.entityType, entityWidth, center, true, true);
            }
        }

        for (const node of state.nodes) {
            if (moving.markers.has(node.uuid) || !selectedMarkers.has(node.uuid))
                continue;
            appendIcon(builder, node.entityType, entityWidth, node, false, true);
        }
    }

    function appendMove(builder) {
        const move = moveState();
        if (!move.active)
            return;

        const entityWidth = Math.max(
            1, Number(state.visualState.entityWidth) || 10);
        if (Array.isArray(move.links)) {
            for (const link of move.links) {
                if (!link || !Array.isArray(link.vertices))
                    continue;
                const vertices = [];
                for (const coordinate of link.vertices) {
                    const point = worldPointFromCoordinate(coordinate);
                    if (point)
                        vertices.push(point);
                }
                if (vertices.length < 2)
                    continue;

                const entityType = Number(link.entity) | 0;
                appendPolyline(builder, vertices, SELECTED_COLOR);
                if (entityType === ENTITY_PIPE) {
                    for (let index = 1; index + 1 < vertices.length; ++index) {
                        appendDisc(
                            builder, vertices[index], PIPE_VERTEX_RADIUS, SELECTED_COLOR);
                    }
                } else if (isDeviceLink(entityType)) {
                    const center = linkCenter(vertices);
                    if (center)
                        appendIcon(builder, entityType, entityWidth, center, true, true);
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
            const point = worldPointFromCoordinate(marker.coordinate);
            if (point)
                appendIcon(builder, entityType, entityWidth, point, false, true);
        }
    }

    function placementEndPoint(placement, targetNode, mapView) {
        if (targetNode)
            return targetNode;
        return worldPointFromScreen(
            Number(placement.mouseX) || 0,
            Number(placement.mouseY) || 0,
            mapView);
    }

    function appendPlacement(builder, mapView) {
        const placement = state.visualState.placement || {};
        if (!placement.creating)
            return;

        const targetUuid = normalizeUuid(placement.connectionTargetUuid);
        const targetNode = state.nodesByUuid.get(targetUuid);
        if (targetNode)
            appendDisc(builder, targetNode, CONNECTION_TARGET_RADIUS, PREVIEW_COLOR);

        const entityType = Number(placement.entity) | 0;
        if (entityType === ENTITY_PIPE && placement.pipeStartNodeUuid) {
            const startNode = state.nodesByUuid.get(
                normalizeUuid(placement.pipeStartNodeUuid));
            if (startNode) {
                const vertices = [startNode];
                if (Array.isArray(placement.pipeIntermediateVertices)) {
                    for (const coordinate of placement.pipeIntermediateVertices) {
                        const point = worldPointFromCoordinate(coordinate);
                        if (point)
                            vertices.push(point);
                    }
                }
                vertices.push(placementEndPoint(placement, targetNode, mapView));
                appendPolyline(builder, vertices, PREVIEW_COLOR);
            }
        }

        let deviceCenter = null;
        if (isDeviceLink(entityType) && placement.deviceLinkStartNodeUuid) {
            const startNode = state.nodesByUuid.get(
                normalizeUuid(placement.deviceLinkStartNodeUuid));
            if (startNode) {
                const end = placementEndPoint(placement, targetNode, mapView);
                deviceCenter = {
                    x: (startNode.x + end.x) / 2,
                    y: (startNode.y + end.y) / 2
                };
                appendSegment(builder, startNode, deviceCenter, PREVIEW_COLOR);
                appendSegment(builder, deviceCenter, end, PREVIEW_COLOR);
            }
        }

        if (!placement.floatingMarkerVisible || entityType === 0
            || Number(placement.floatingWidth) <= 0) {
            return;
        }
        const width = Number(placement.floatingWidth);
        if (deviceCenter) {
            appendIcon(builder, entityType, width, deviceCenter, true, false);
            return;
        }
        const point = worldPointFromScreen(
            Number(placement.mouseX) || 0,
            Number(placement.mouseY) || 0,
            mapView);
        appendIcon(builder, entityType, width, point, false, false);
    }

    function buildOverlayBatch(mapView) {
        const builder = createBatchBuilder();
        if (state.geometryReady) {
            appendSelectedNetwork(builder);
            appendMove(builder);
            appendPlacement(builder, mapView);
        }
        commitBatch("overlay", builder);
    }

    function synchronizeNetworkRenderer(mapView) {
        if (state.spriteAtlasDirty) {
            const sprites = [];
            for (const entry of state.iconImages.values()) {
                if (entry.loaded)
                    sprites.push({ slot: entry.slot, image: entry.image });
            }
            state.networkRenderer.setSpriteImages(sprites);
            state.spriteAtlasDirty = false;
        }
        if (state.baseDirty) {
            buildBaseBatch();
            state.baseDirty = false;
        }
        if (state.overlayDirty) {
            buildOverlayBatch(mapView);
            state.overlayDirty = false;
        }
    }

    function renderNetwork() {
        state.networkRenderFrameRequest = 0;
        const mapView = state.lastMapView;
        if (!state.networkRenderer || !shouldDisplay(mapView)) {
            if (state.networkRenderer)
                state.networkRenderer.clear();
            return;
        }

        try {
            synchronizeNetworkRenderer(mapView);
            if (!state.geometryReady) {
                state.networkRenderer.clear();
                return;
            }
            const transform = worldTransform(mapView);
            const rendered = state.networkRenderer.render({
                width: mapView.width,
                height: mapView.height,
                pixelRatio: Math.min(
                    devicePixelRatio(), NETWORK_MAX_PIXEL_RATIO),
                translateX: transform.translateX,
                translateY: transform.translateY,
                scale: transform.scale,
                batches: {
                    base: { segmentWidth: 3, discScale: 1, spriteScale: 1 },
                    overlay: { segmentWidth: 3, discScale: 1, spriteScale: 1 }
                }
            });
            if (!rendered && state.networkCanvas)
                state.networkCanvas.style.display = "none";
        } catch (error) {
            disableNetworkRenderer(error);
        }
    }

    function scheduleNetworkRender() {
        if (!state.lastMapView || state.networkRenderFrameRequest !== 0)
            return;
        state.networkRenderFrameRequest = window.requestAnimationFrame(renderNetwork);
    }

    function clearScheduledNetworkRender() {
        if (state.networkRenderFrameRequest === 0)
            return;
        window.cancelAnimationFrame(state.networkRenderFrameRequest);
        state.networkRenderFrameRequest = 0;
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
        const wrapShift = Math.round(
            (centerTileX - selectionCenter) / worldTileCount) * worldTileCount;
        westTile += wrapShift;
        eastTile += wrapShift;

        const left = mapView.width / 2 + (westTile - centerTileX) * tileSize;
        const right = mapView.width / 2 + (eastTile - centerTileX) * tileSize;
        const top = mapView.height / 2 + (northTile - centerTileY) * tileSize;
        const bottom = mapView.height / 2 + (southTile - centerTileY) * tileSize;
        const width = right - left;
        const height = bottom - top;
        if (width <= 0 || height <= 0 || right < 0 || bottom < 0
            || left > mapView.width || top > mapView.height) {
            return;
        }

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
        const firstVisibleX = Math.max(
            Number(tile.xMin) + 1, Math.ceil(viewportWestTile - wrapShift));
        const lastVisibleX = Math.min(
            Number(tile.xMax), Math.floor(viewportEastTile - wrapShift));
        for (let tileX = firstVisibleX; tileX <= lastVisibleX; ++tileX) {
            const x = mapView.width / 2
                + (tileX + wrapShift - centerTileX) * tileSize;
            context.beginPath();
            context.moveTo(x, top);
            context.lineTo(x, bottom);
            context.stroke();
        }

        const viewportNorthTile = centerTileY - mapView.height / 2 / tileSize;
        const viewportSouthTile = centerTileY + mapView.height / 2 / tileSize;
        const firstVisibleY = Math.max(
            Number(tile.yMin) + 1, Math.ceil(viewportNorthTile));
        const lastVisibleY = Math.min(
            Number(tile.yMax), Math.floor(viewportSouthTile));
        for (let tileY = firstVisibleY; tileY <= lastVisibleY; ++tileY) {
            const y = mapView.height / 2 + (tileY - centerTileY) * tileSize;
            context.beginPath();
            context.moveTo(left, y);
            context.lineTo(right, y);
            context.stroke();
        }
    }

    function updateRectangleSelectionElement() {
        const element = state.rectangleSelectionElement;
        const rectangle = state.viewportState.rectangleSelection;
        if (!element)
            return;
        if (!rectangle || !rectangle.visible
            || rectangle.width <= 0 || rectangle.height <= 0) {
            element.style.display = "none";
            return;
        }

        const x = Number(rectangle.x) + 2.5;
        const y = Number(rectangle.y) + 2.5;
        const width = Number(rectangle.width) - 5;
        const height = Number(rectangle.height) - 5;
        if (width <= 0 || height <= 0) {
            element.style.display = "none";
            return;
        }
        element.style.transform = `translate3d(${x}px, ${y}px, 0)`;
        element.style.width = `${width}px`;
        element.style.height = `${height}px`;
        element.style.display = "block";
    }

    function renderUnderlay() {
        state.underlayRenderPending = false;
        const mapView = state.lastMapView;
        if (!state.underlayCanvas || !shouldDisplay(mapView))
            return;
        const context = viewportContext(
            state.underlayCanvas, mapView.width, mapView.height);
        drawTileSelection(context, mapView);
    }

    function scheduleUnderlayRender() {
        if (state.underlayRenderPending)
            return;
        state.underlayRenderPending = true;
        window.requestAnimationFrame(renderUnderlay);
    }

    function tileSelectionsEqual(first, second) {
        return Boolean(first && second
            && Boolean(first.visible) === Boolean(second.visible)
            && Number(first.xMin) === Number(second.xMin)
            && Number(first.xMax) === Number(second.xMax)
            && Number(first.yMin) === Number(second.yMin)
            && Number(first.yMax) === Number(second.yMax));
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

        resizeViewportCanvas(
            state.underlayCanvas, mapView.width, mapView.height);
        const placement = state.visualState.placement || {};
        if (placement.creating)
            state.overlayDirty = true;
        updateRectangleSelectionElement();
        const tileSelection = state.viewportState.tileSelection;
        if (tileSelection && tileSelection.visible)
            scheduleUnderlayRender();
        scheduleNetworkRender();
    }

    function setNetworkSnapshot(snapshot) {
        if (!snapshot || !Array.isArray(snapshot.nodes) || !Array.isArray(snapshot.links))
            throw new TypeError("Invalid AOWIS map editor network snapshot");
        state.networkSnapshot = snapshot;
        parseNetworkGeometry();
        state.baseDirty = true;
        state.overlayDirty = true;
        scheduleNetworkRender();
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
        if (!state.networkSnapshot || !patch
            || !Array.isArray(patch.nodes) || !Array.isArray(patch.links)) {
            throw new TypeError("Invalid AOWIS map editor geometry patch");
        }

        replaceSnapshotItems(state.networkSnapshot.nodes, patch.nodes, "node");
        replaceSnapshotItems(state.networkSnapshot.links, patch.links, "link");
        state.networkSnapshot.geometryRevision = patch.geometryRevision;
        state.networkSnapshot.visualRevision = patch.visualRevision;
        parseNetworkGeometry();
        state.baseDirty = true;
        state.overlayDirty = true;
        scheduleNetworkRender();
    }

    function setVisualState(visualState) {
        if (!visualState || typeof visualState !== "object")
            throw new TypeError("Invalid AOWIS map editor visual state");
        const previousWrapReference =
            Number(state.visualState.wrapReferenceLongitude) || 0;
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
        if (wrapReference !== previousWrapReference
            || wrapReference !== state.geometryWrapReferenceLongitude) {
            parseNetworkGeometry();
            state.baseDirty = true;
        } else if (entityWidth !== previousEntityWidth
                   || previousMoveActive !== nextMoveActive
                   || (nextMoveActive
                       && previousMoveSessionId !== nextMoveSessionId)) {
            state.baseDirty = true;
        }
        state.overlayDirty = true;
        scheduleNetworkRender();
    }

    function setViewportState(viewportState) {
        if (!viewportState || typeof viewportState !== "object")
            throw new TypeError("Invalid AOWIS map editor viewport state");
        const previousTileSelection = state.viewportState.tileSelection;
        state.viewportState = viewportState;
        applyBackground();
        updateRectangleSelectionElement();
        const tileSelection = viewportState.tileSelection;
        if (!tileSelectionsEqual(previousTileSelection, tileSelection))
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

    function clearUnderlay() {
        if (!state.underlayCanvas)
            return;
        const context = state.underlayCanvas.getContext("2d");
        context.setTransform(1, 0, 0, 1, 0, 0);
        context.clearRect(
            0, 0, state.underlayCanvas.width, state.underlayCanvas.height);
    }

    function clear() {
        clearScheduledNetworkRender();
        state.networkSnapshot = null;
        state.visualState = defaultVisualState();
        state.viewportState = defaultViewportState();
        state.nodes = [];
        state.links = [];
        state.nodesByUuid.clear();
        state.geometryReady = false;
        state.geometryOriginX = 0;
        state.geometryOriginY = 0;
        state.geometryWrapReferenceLongitude = Number.NaN;
        state.baseDirty = true;
        state.overlayDirty = true;
        if (state.networkRenderer)
            state.networkRenderer.clear();
        updateRectangleSelectionElement();
        clearUnderlay();
        applyBackground();
    }

    function destroy() {
        if (state.unsubscribeView)
            state.unsubscribeView();
        state.unsubscribeView = null;
        clearScheduledNetworkRender();
        ++state.iconLoadGeneration;
        if (state.networkRenderer)
            state.networkRenderer.destroy();
        state.networkRenderer = null;
        state.networkCanvas = null;
        if (state.layer)
            state.layer.remove();
        state.layer = null;
        state.underlayCanvas = null;
        state.rectangleSelectionElement = null;
        state.iconImages.clear();
        state.networkWebGlUnavailable = false;
        state.lastMapView = null;
        clear();
    }

    function initialize() {
        if (!window.aowisBrowserMap
            || typeof window.aowisBrowserMap.subscribeView !== "function") {
            throw new Error("AOWIS browser map editor requires aowis-browser-map.js");
        }
        state.unsubscribeView =
            window.aowisBrowserMap.subscribeView(handleMapViewChanged);
        loadIconImages();
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
