(function () {
    "use strict";

    const SHARED_RENDERER = window.aowisBrowserVector;
    if (!SHARED_RENDERER)
        throw new Error("AOWIS browser network requires aowis-browser-vector.js");

    const TILE_SIZE = 256;
    const REFERENCE_ZOOM = SHARED_RENDERER.REFERENCE_ZOOM;
    const NETWORK_COLOR = "#000000";
    const SYMBOLOGY_VALUE_UNAVAILABLE_COLOR = "#000000";
    const SELECTED_COLOR = "rgb(0, 190, 255)";
    const SYMBOLOGY_COLOR_BUCKETS = 64;
    const RAMP_COLORS = [
        "#440154",
        "#443983",
        "#31688e",
        "#21918c",
        "#35b779",
        "#90d743",
        "#fde725"
    ];
    const NETWORK_IMAGE_PADDING = 64;
    const NETWORK_STATIC_MAX_PIXEL_RATIO = 1.5;
    const NETWORK_IMAGE_OVERSCAN_FACTOR = 3;
    const NETWORK_IMAGE_MAX_DIMENSION = 4096;
    const NETWORK_IMAGE_MAX_AREA = 8 * 1024 * 1024;
    const NETWORK_IMAGE_REBUILD_EDGE = 256;
    const HEATMAP_OVERSCAN_FACTOR = 1.5;
    const HEATMAP_RASTER_SCALE = 0.5;
    const HEATMAP_WEBGL_MAX_PIXEL_RATIO = 0.5;
    const HEATMAP_WEBGL_MIN_PIXEL_RATIO = 0.0625;
    const HEATMAP_WEBGL_TARGET_RADIUS_PIXELS = 96;
    const HEATMAP_WEBGL_TARGET_FRAGMENT_BUDGET = 24 * 1024 * 1024;
    const HEATMAP_REBUILD_EDGE = 96;
    const HEATMAP_MAX_DIMENSION = 1536;
    const HEATMAP_MAX_AREA = 1280 * 1024;
    const HEATMAP_MAX_KERNEL_CACHE_PIXELS = 8 * 1024 * 1024;
    const HEATMAP_MAX_KERNEL_RADIUS = 256;
    const HEATMAP_MAX_COLOR_BUCKETS = 64;
    const MAX_SPATIAL_QUERY_CELLS = 4096;
    const LINK_HIT_DISTANCE = 7;
    const SPATIAL_CELL_SIZE = 128;
    const ENTITY_JUNCTION = SHARED_RENDERER.ENTITY_JUNCTION;
    const ENTITY_RESERVOIR = SHARED_RENDERER.ENTITY_RESERVOIR;
    const ENTITY_TANK = SHARED_RENDERER.ENTITY_TANK;
    const ENTITY_PIPE = SHARED_RENDERER.ENTITY_PIPE;
    const ENTITY_PUMP = SHARED_RENDERER.ENTITY_PUMP;
    const ENTITY_VALVE = SHARED_RENDERER.ENTITY_VALVE;
    const ICON_DEFINITIONS = new Map([
        [ENTITY_RESERVOIR, {
            file: "svg/reservoir.svg",
            viewWidth: 186,
            viewHeight: 138,
            hitShape: "rectangle"
        }],
        [ENTITY_TANK, {
            file: "svg/tank.svg",
            viewWidth: 138,
            viewHeight: 183,
            hitShape: "rectangle"
        }],
        [ENTITY_PUMP, {
            file: "svg/pump.svg",
            viewWidth: 126,
            viewHeight: 110,
            hitShape: "rectangle"
        }],
        [ENTITY_VALVE, {
            file: "svg/valve.svg",
            viewWidth: 138,
            viewHeight: 138,
            hitShape: "ellipse"
        }]
    ]);

    const state = {
        layer: null,
        networkImage: null,
        networkImageObjectUrl: null,
        networkImageZoom: null,
        networkImageOriginX: 0,
        networkImageOriginY: 0,
        networkImageStyleRevision: 0,
        pendingNetworkImage: null,
        pendingNetworkImageObjectUrl: null,
        pendingNetworkImageZoom: null,
        pendingNetworkImageStyleRevision: 0,
        networkImageGeneration: 0,
        networkStyleRevision: 1,
        networkSvgCacheRevision: 0,
        networkSvgLinkPaths: [],
        networkSvgJunctionPaths: [],
        networkSvgIcons: [],
        selectionCanvas: null,
        selectionRenderPending: false,
        selectedRenderId: 0,
        selectedEntityType: 0,
        entityMarkers: new Map(),
        entitySegments: new Map(),
        unsubscribeView: null,
        geometryOriginX: 0,
        geometryOriginY: 0,
        geometryMinimumX: 0,
        geometryMinimumY: 0,
        geometryMaximumX: 0,
        geometryMaximumY: 0,
        iconImages: new Map(),
        iconSvgTexts: new Map(),
        coloredIconSvgDataUrls: new Map(),
        tintedIconCache: new Map(),
        geometryReady: false,
        width: 0,
        height: 0,
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
        ownerId: 0,
        nodeVisual: 0,
        nodeSizePercent: 100,
        iconSizePercent: 100,
        nodeMinimum: 0,
        nodeMaximum: 0,
        nodeValues: new Map(),
        nodeColors: new Map(),
        linkVisual: 0,
        linkThicknessPixels: 3,
        linkMinimum: 0,
        linkMaximum: 0,
        linkValues: new Map(),
        linkColors: new Map(),
        heatmapCanvas: null,
        heatmapMode: null,
        heatmapGl: null,
        heatmapGlProgram: null,
        heatmapGlBuffer: null,
        heatmapGlLocations: null,
        heatmapGlVertexCount: 0,
        heatmapGlNodeCount: 0,
        heatmapDataRevision: 1,
        heatmapUploadedRevision: 0,
        heatmapWebGlFailureLogged: false,
        heatmapCacheCanvas: null,
        heatmapZoom: null,
        heatmapOffsetX: 0,
        heatmapOffsetY: 0,
        heatmapWidth: 0,
        heatmapHeight: 0,
        heatmapRasterScale: 1,
        heatmapBounds: null,
        heatmapFrameRequest: 0,
        heatmapPresentationFrameRequest: 0,
        heatmapKernelCache: new Map(),
        heatmapVisual: 0,
        heatmapMinimum: 0,
        heatmapMaximum: 0,
        heatmapValues: new Map(),
        heatmapOpacity: 75,
        heatmapRadiusMeters: 400,
        heatmapSolidCenterPercent: 70
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
            state.selectionCanvas = SHARED_RENDERER.createCanvas(3);
            state.selectionCanvas.style.willChange = "transform";
            state.layer.appendChild(state.selectionCanvas);
            applyBackground();
        }

        if (state.layer.parentElement !== mapLayer)
            mapLayer.appendChild(state.layer);

        return true;
    }

    function scaleForZoom(zoom) {
        return Math.pow(2, zoom - REFERENCE_ZOOM);
    }

    function nodeSizeScale() {
        return Math.max(0.5, Math.min(2.5, state.nodeSizePercent / 100));
    }

    function iconSizeScale() {
        return Math.max(0.5, Math.min(2.5, state.iconSizePercent / 100));
    }

    function baseMarkerSizeForZoom(zoom) {
        return Math.max(10, Math.min(40, 10 + (zoom - 16) * 10));
    }

    function nodeMarkerSizeForZoom(zoom) {
        return Math.max(5, baseMarkerSizeForZoom(zoom) * nodeSizeScale());
    }

    function iconMarkerSizeForZoom(zoom) {
        return Math.max(5, baseMarkerSizeForZoom(zoom) * iconSizeScale());
    }

    function maximumMarkerSizeForZoom(zoom) {
        return Math.max(nodeMarkerSizeForZoom(zoom), iconMarkerSizeForZoom(zoom));
    }

    function junctionDotDiameterForZoom(zoom) {
        const baseDiameter = Math.max(
            8, Math.min(12, baseMarkerSizeForZoom(zoom) * 0.3));
        return Math.max(4, baseDiameter * nodeSizeScale());
    }

    function linkHitDistance() {
        return Math.max(LINK_HIT_DISTANCE, state.linkThicknessPixels / 2 + 3);
    }

    function markerScreenBounds(entityType, zoom) {
        const markerSize = entityType === ENTITY_JUNCTION
            ? nodeMarkerSizeForZoom(zoom) : iconMarkerSizeForZoom(zoom);
        if (entityType === ENTITY_JUNCTION) {
            return {
                width: markerSize,
                height: markerSize,
                hitShape: "ellipse"
            };
        }

        const definition = ICON_DEFINITIONS.get(entityType);
        if (!definition) {
            return {
                width: markerSize,
                height: markerSize,
                hitShape: "ellipse"
            };
        }

        const maximumViewDimension = Math.max(definition.viewWidth, definition.viewHeight);
        return {
            width: markerSize * definition.viewWidth / maximumViewDimension,
            height: markerSize * definition.viewHeight / maximumViewDimension,
            hitShape: definition.hitShape
        };
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

    function worldPixelToLatitude(worldPixelY) {
        const normalized = 1 - 2 * worldPixelY / worldSize(REFERENCE_ZOOM);
        const mercator = normalized * Math.PI;
        const radians = 2 * Math.atan(Math.exp(mercator)) - Math.PI / 2;
        return radians * 180 / Math.PI;
    }

    function metersPerReferencePixel(latitude) {
        const latitudeRadians = clampLatitude(latitude) * Math.PI / 180;
        return 156543.03392804097 * Math.cos(latitudeRadians)
            / Math.pow(2, REFERENCE_ZOOM);
    }

    function nearestWrappedWorldPixel(rawPixelX, referencePixelX) {
        const size = worldSize(REFERENCE_ZOOM);
        return rawPixelX + Math.round((referencePixelX - rawPixelX) / size) * size;
    }

    function formatted(value) {
        return String(Math.round(value * 1000) / 1000);
    }

    function formattedScale(value) {
        return value.toFixed(15).replace(/0+$/, "").replace(/\.$/, "");
    }

    function hexadecimalByte(value) {
        return Math.max(0, Math.min(255, Math.round(value))).toString(16).padStart(2, "0");
    }

    function rampRgb(fraction) {
        const limitedFraction = Math.max(0, Math.min(1, Number(fraction) || 0));
        const scaled = limitedFraction * (RAMP_COLORS.length - 1);
        const leftIndex = Math.min(RAMP_COLORS.length - 1, Math.floor(scaled));
        const rightIndex = Math.min(RAMP_COLORS.length - 1, leftIndex + 1);
        const ratio = scaled - leftIndex;
        const left = RAMP_COLORS[leftIndex];
        const right = RAMP_COLORS[rightIndex];
        const leftRed = Number.parseInt(left.slice(1, 3), 16);
        const leftGreen = Number.parseInt(left.slice(3, 5), 16);
        const leftBlue = Number.parseInt(left.slice(5, 7), 16);
        const rightRed = Number.parseInt(right.slice(1, 3), 16);
        const rightGreen = Number.parseInt(right.slice(3, 5), 16);
        const rightBlue = Number.parseInt(right.slice(5, 7), 16);
        return {
            red: leftRed + (rightRed - leftRed) * ratio,
            green: leftGreen + (rightGreen - leftGreen) * ratio,
            blue: leftBlue + (rightBlue - leftBlue) * ratio
        };
    }

    function rampColor(fraction) {
        const color = rampRgb(fraction);
        return `#${hexadecimalByte(color.red)}${hexadecimalByte(color.green)}${hexadecimalByte(color.blue)}`;
    }

    const SYMBOLOGY_PALETTE = Array.from({ length: SYMBOLOGY_COLOR_BUCKETS }, (_, index) =>
        rampColor(index / Math.max(1, SYMBOLOGY_COLOR_BUCKETS - 1)));

    function quantizedRampColor(value, minimum, maximum) {
        if (!Number.isFinite(value) || !Number.isFinite(minimum) || !Number.isFinite(maximum))
            return SYMBOLOGY_VALUE_UNAVAILABLE_COLOR;
        if (minimum === maximum)
            return SYMBOLOGY_PALETTE[Math.floor((SYMBOLOGY_COLOR_BUCKETS - 1) / 2)];

        const fraction = Math.max(0, Math.min(1, (value - minimum) / (maximum - minimum)));
        const bucket = Math.max(0, Math.min(
            SYMBOLOGY_COLOR_BUCKETS - 1,
            Math.round(fraction * (SYMBOLOGY_COLOR_BUCKETS - 1))));
        return SYMBOLOGY_PALETTE[bucket];
    }

    function buildSymbologyColorMap(visual, values, minimum, maximum) {
        const colors = new Map();
        if (visual === 0)
            return colors;

        for (const [renderId, value] of values)
            colors.set(renderId, quantizedRampColor(value, minimum, maximum));
        return colors;
    }

    function nodeColor(renderId) {
        if (state.nodeVisual === 0)
            return NETWORK_COLOR;
        return state.nodeColors.get(renderId) || SYMBOLOGY_VALUE_UNAVAILABLE_COLOR;
    }

    function linkColor(renderId) {
        if (state.linkVisual === 0)
            return NETWORK_COLOR;
        return state.linkColors.get(renderId) || SYMBOLOGY_VALUE_UNAVAILABLE_COLOR;
    }

    function markerColor(marker) {
        if (marker.entityType === ENTITY_JUNCTION || marker.entityType === ENTITY_RESERVOIR
            || marker.entityType === ENTITY_TANK) {
            return nodeColor(marker.renderId);
        }
        return linkColor(marker.renderId);
    }

    function devicePixelRatio() {
        return Math.max(1, window.devicePixelRatio || 1);
    }

    function snapToPhysicalPixel(value) {
        const ratio = devicePixelRatio();
        return Math.round(value * ratio) / ratio;
    }

    function vectorRenderer() {
        return SHARED_RENDERER;
    }

    function revokeObjectUrl(objectUrl) {
        if (objectUrl)
            URL.revokeObjectURL(objectUrl);
    }

    function clearPendingNetworkImage() {
        ++state.networkImageGeneration;
        if (state.pendingNetworkImage)
            state.pendingNetworkImage.remove();
        revokeObjectUrl(state.pendingNetworkImageObjectUrl);
        state.pendingNetworkImage = null;
        state.pendingNetworkImageObjectUrl = null;
        state.pendingNetworkImageZoom = null;
        state.pendingNetworkImageStyleRevision = 0;
    }

    function clearRenderedNetworkImage() {
        if (state.networkImage)
            state.networkImage.remove();
        revokeObjectUrl(state.networkImageObjectUrl);
        state.networkImage = null;
        state.networkImageObjectUrl = null;
        state.networkImageZoom = null;
        state.networkImageOriginX = 0;
        state.networkImageOriginY = 0;
        state.networkImageStyleRevision = 0;
    }

    function resetNetworkImage(clearRendered) {
        clearPendingNetworkImage();
        if (clearRendered)
            clearRenderedNetworkImage();
        state.networkSvgCacheRevision = 0;
        state.networkSvgLinkPaths = [];
        state.networkSvgJunctionPaths = [];
        state.networkSvgIcons = [];
    }

    function invalidateNetworkCache() {
        ++state.networkStyleRevision;
        state.networkSvgCacheRevision = 0;
        state.tintedIconCache.clear();
        if (state.lastMapView)
            requestNetworkImage(state.lastMapView.zoom);
    }

    function utf8Base64(text) {
        const bytes = new TextEncoder().encode(text);
        const chunkSize = 0x8000;
        let binary = "";
        for (let offset = 0; offset < bytes.length; offset += chunkSize)
            binary += String.fromCharCode(...bytes.subarray(offset, offset + chunkSize));
        return window.btoa(binary);
    }

    function coloredIconSvgDataUrl(entityType, color) {
        const source = state.iconSvgTexts.get(entityType);
        if (!source)
            return null;

        const key = `${entityType}:${color}`;
        let dataUrl = state.coloredIconSvgDataUrls.get(key);
        if (dataUrl)
            return dataUrl;

        const colored = source.replace(/#000000/gi, color);
        dataUrl = `data:image/svg+xml;base64,${utf8Base64(colored)}`;
        state.coloredIconSvgDataUrls.set(key, dataUrl);
        return dataUrl;
    }

    function escapeXmlAttribute(value) {
        return String(value).replace(/&/g, "&amp;").replace(/"/g, "&quot;")
            .replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }

    function buildNetworkSvgCache() {
        if (state.networkSvgCacheRevision === state.networkStyleRevision)
            return;

        const linkCommandsByColor = new Map();
        const appendSegment = (segment) => {
            const color = linkColor(segment.renderId);
            let commands = linkCommandsByColor.get(color);
            if (!commands) {
                commands = [];
                linkCommandsByColor.set(color, commands);
            }
            commands.push(
                "M", formatted(segment.x1 - state.geometryMinimumX), " ",
                formatted(segment.y1 - state.geometryMinimumY), "L",
                formatted(segment.x2 - state.geometryMinimumX), " ",
                formatted(segment.y2 - state.geometryMinimumY));
        };
        for (const segment of state.pipeSegments)
            appendSegment(segment);
        for (const segment of state.deviceSegments)
            appendSegment(segment);

        const junctionCommandsByColor = new Map();
        const icons = [];
        for (const marker of state.markers) {
            const color = markerColor(marker);
            const localX = marker.x - state.geometryMinimumX;
            const localY = marker.y - state.geometryMinimumY;
            if (marker.entityType === ENTITY_JUNCTION) {
                let commands = junctionCommandsByColor.get(color);
                if (!commands) {
                    commands = [];
                    junctionCommandsByColor.set(color, commands);
                }
                commands.push("M", formatted(localX), " ", formatted(localY), "l0.001 0");
                continue;
            }
            icons.push({
                entityType: marker.entityType,
                x: localX,
                y: localY,
                color: color
            });
        }

        state.networkSvgLinkPaths = Array.from(linkCommandsByColor, ([color, commands]) => ({
            color: color,
            data: commands.join("")
        }));
        state.networkSvgJunctionPaths = Array.from(junctionCommandsByColor, ([color, commands]) => ({
            color: color,
            data: commands.join("")
        }));
        state.networkSvgIcons = icons;
        state.networkSvgCacheRevision = state.networkStyleRevision;
    }

    function networkImageSpecification(zoom) {
        buildNetworkSvgCache();
        const scale = scaleForZoom(zoom);
        const markerPadding = maximumMarkerSizeForZoom(zoom) / 2 + 4;
        const padding = Math.max(NETWORK_IMAGE_PADDING, markerPadding, state.linkThicknessPixels + 4);
        const worldWidth = Math.max(0, state.geometryMaximumX - state.geometryMinimumX);
        const worldHeight = Math.max(0, state.geometryMaximumY - state.geometryMinimumY);
        const width = Math.max(1, Math.ceil(worldWidth * scale + padding * 2));
        const height = Math.max(1, Math.ceil(worldHeight * scale + padding * 2));
        const parts = [
            `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}">`,
            `<g transform="translate(${formatted(padding)} ${formatted(padding)}) scale(${formattedScale(scale)})">`
        ];

        for (const path of state.networkSvgLinkPaths) {
            parts.push(`<path fill="none" stroke="${path.color}" stroke-width="${state.linkThicknessPixels}" `
                + `stroke-linecap="round" stroke-linejoin="round" vector-effect="non-scaling-stroke" d="${path.data}"/>`);
        }

        const junctionDiameter = junctionDotDiameterForZoom(zoom);
        for (const path of state.networkSvgJunctionPaths) {
            parts.push(`<path fill="none" stroke="${path.color}" stroke-width="${formatted(junctionDiameter)}" `
                + `stroke-linecap="round" vector-effect="non-scaling-stroke" d="${path.data}"/>`);
        }

        for (const icon of state.networkSvgIcons) {
            const definition = ICON_DEFINITIONS.get(icon.entityType);
            if (!definition)
                continue;
            const dataUrl = coloredIconSvgDataUrl(icon.entityType, icon.color);
            if (!dataUrl)
                continue;
            const bounds = markerScreenBounds(icon.entityType, zoom);
            const localWidth = bounds.width / scale;
            const localHeight = bounds.height / scale;
            const x = icon.x - localWidth / 2;
            const y = icon.y - localHeight / 2;
            parts.push(`<image x="${formatted(x)}" y="${formatted(y)}" width="${formatted(localWidth)}" `
                + `height="${formatted(localHeight)}" href="${escapeXmlAttribute(dataUrl)}"/>`);
        }

        parts.push("</g></svg>");
        return {
            svg: parts.join(""),
            width: width,
            height: height,
            originX: state.geometryMinimumX - padding / scale,
            originY: state.geometryMinimumY - padding / scale,
            styleRevision: state.networkStyleRevision
        };
    }

    function positionNetworkImage(mapView) {
        if (!state.networkImage || state.networkImageZoom === null)
            return;

        const renderedScale = scaleForZoom(state.networkImageZoom);
        const currentScale = scaleForZoom(mapView.zoom);
        const zoomScale = currentScale / renderedScale;
        const transform = worldTransform(mapView);
        const left = snapToPhysicalPixel(
            transform.translateX + (state.networkImageOriginX - state.geometryOriginX) * currentScale);
        const top = snapToPhysicalPixel(
            transform.translateY + (state.networkImageOriginY - state.geometryOriginY) * currentScale);
        state.networkImage.style.display = shouldDisplayNetwork(mapView) ? "block" : "none";
        state.networkImage.style.transform =
            `translate3d(${left}px, ${top}px, 0) scale(${zoomScale})`;
    }

    function requestNetworkImage(zoom) {
        if (!state.layer || !state.geometryReady)
            return;
        if (state.networkImage && state.networkImageZoom === zoom
            && state.networkImageStyleRevision === state.networkStyleRevision) {
            positionNetworkImage(state.lastMapView);
            return;
        }
        if (state.pendingNetworkImage && state.pendingNetworkImageZoom === zoom
            && state.pendingNetworkImageStyleRevision === state.networkStyleRevision) {
            return;
        }

        clearPendingNetworkImage();
        const generation = state.networkImageGeneration;
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
        image.style.zIndex = "2";
        image.style.transformOrigin = "0 0";
        image.style.willChange = "transform";
        image.style.backfaceVisibility = "hidden";

        state.pendingNetworkImage = image;
        state.pendingNetworkImageObjectUrl = objectUrl;
        state.pendingNetworkImageZoom = zoom;
        state.pendingNetworkImageStyleRevision = specification.styleRevision;

        image.addEventListener("load", () => {
            if (generation !== state.networkImageGeneration || state.pendingNetworkImage !== image) {
                revokeObjectUrl(objectUrl);
                return;
            }
            if (specification.styleRevision !== state.networkStyleRevision) {
                state.pendingNetworkImage = null;
                state.pendingNetworkImageObjectUrl = null;
                state.pendingNetworkImageZoom = null;
                state.pendingNetworkImageStyleRevision = 0;
                revokeObjectUrl(objectUrl);
                requestNetworkImage(state.lastMapView ? state.lastMapView.zoom : zoom);
                return;
            }

            clearRenderedNetworkImage();
            state.pendingNetworkImage = null;
            state.pendingNetworkImageObjectUrl = null;
            state.pendingNetworkImageZoom = null;
            state.pendingNetworkImageStyleRevision = 0;
            state.networkImage = image;
            state.networkImageObjectUrl = objectUrl;
            state.networkImageZoom = zoom;
            state.networkImageOriginX = specification.originX;
            state.networkImageOriginY = specification.originY;
            state.networkImageStyleRevision = specification.styleRevision;
            state.layer.appendChild(image);
            if (state.lastMapView) {
                positionNetworkImage(state.lastMapView);
                if (state.lastMapView.zoom !== zoom)
                    requestNetworkImage(state.lastMapView.zoom);
            }
        }, { once: true });

        image.addEventListener("error", () => {
            if (state.pendingNetworkImage === image) {
                state.pendingNetworkImage = null;
                state.pendingNetworkImageObjectUrl = null;
                state.pendingNetworkImageZoom = null;
                state.pendingNetworkImageStyleRevision = 0;
            }
            revokeObjectUrl(objectUrl);
            console.error("Failed to load AOWIS monitor network SVG image");
        }, { once: true });

        image.src = objectUrl;
    }

    function clearScheduledHeatmap() {
        if (state.heatmapFrameRequest !== 0)
            window.cancelAnimationFrame(state.heatmapFrameRequest);
        if (state.heatmapPresentationFrameRequest !== 0)
            window.cancelAnimationFrame(state.heatmapPresentationFrameRequest);
        state.heatmapFrameRequest = 0;
        state.heatmapPresentationFrameRequest = 0;
    }

    function clearRenderedHeatmap() {
        state.heatmapCacheCanvas = null;
        state.heatmapZoom = null;
        state.heatmapOffsetX = 0;
        state.heatmapOffsetY = 0;
        state.heatmapWidth = 0;
        state.heatmapHeight = 0;
        state.heatmapRasterScale = 1;
        state.heatmapBounds = null;
        if (state.heatmapCanvas) {
            if (state.heatmapMode === "webgl" && state.heatmapGl) {
                state.heatmapGl.clearColor(0, 0, 0, 0);
                state.heatmapGl.clear(state.heatmapGl.COLOR_BUFFER_BIT);
            } else if (state.heatmapMode === "canvas") {
                const context = state.heatmapCanvas.getContext("2d");
                if (context)
                    context.clearRect(0, 0, state.heatmapCanvas.width, state.heatmapCanvas.height);
            }
            state.heatmapCanvas.style.display = "none";
        }
    }

    function clearHeatmap() {
        clearScheduledHeatmap();
        clearRenderedHeatmap();
        state.heatmapKernelCache.clear();
        ++state.heatmapDataRevision;
        state.heatmapUploadedRevision = 0;
    }

    function loadIconImage(entityType, definition) {
        return new Promise((resolve) => {
            const image = new Image();
            image.decoding = "async";
            image.onload = function () {
                state.iconImages.set(entityType, { image: image, loaded: true });
                resolve();
            };
            image.onerror = function () {
                console.error(`Failed to load AOWIS network icon ${definition.file}`);
                state.iconImages.set(entityType, { image: image, loaded: false });
                resolve();
            };
            image.src = definition.file;
        });
    }

    async function loadIconSvgText(entityType, definition) {
        try {
            const response = await fetch(definition.file);
            if (!response.ok)
                throw new Error(`HTTP ${response.status}`);
            state.iconSvgTexts.set(entityType, await response.text());
        } catch (error) {
            console.error(`Failed to load AOWIS network SVG ${definition.file}:`, error);
        }
    }

    async function loadIconImages() {
        const requests = [];
        for (const [entityType, definition] of ICON_DEFINITIONS) {
            requests.push(loadIconImage(entityType, definition));
            requests.push(loadIconSvgText(entityType, definition));
        }
        await Promise.all(requests);
        state.coloredIconSvgDataUrls.clear();
        invalidateNetworkCache();
        scheduleSelectionRender();
    }

    function tintedIcon(entityType, width, height, color) {
        const entry = state.iconImages.get(entityType);
        if (!entry || !entry.loaded || !(width > 0) || !(height > 0))
            return null;

        const ratio = Math.min(devicePixelRatio(), NETWORK_STATIC_MAX_PIXEL_RATIO);
        const physicalWidth = Math.max(1, Math.ceil(width * ratio));
        const physicalHeight = Math.max(1, Math.ceil(height * ratio));
        const key = `${entityType}:${physicalWidth}:${physicalHeight}:${color}`;
        let canvas = state.tintedIconCache.get(key);
        if (canvas)
            return canvas;

        canvas = document.createElement("canvas");
        canvas.width = physicalWidth;
        canvas.height = physicalHeight;
        const context = canvas.getContext("2d");
        if (!context)
            return null;
        context.setTransform(ratio, 0, 0, ratio, 0, 0);
        context.clearRect(0, 0, width, height);
        context.drawImage(entry.image, 0, 0, width, height);
        context.globalCompositeOperation = "source-in";
        context.fillStyle = color;
        context.fillRect(0, 0, width, height);
        context.globalCompositeOperation = "source-over";
        state.tintedIconCache.set(key, canvas);
        return canvas;
    }

    function boundedImageDimensions(mapView) {
        const viewportArea = Math.max(1, mapView.width * mapView.height);
        const maximumFactor = Math.min(
            NETWORK_IMAGE_OVERSCAN_FACTOR,
            NETWORK_IMAGE_MAX_DIMENSION / Math.max(1, mapView.width),
            NETWORK_IMAGE_MAX_DIMENSION / Math.max(1, mapView.height),
            Math.sqrt(NETWORK_IMAGE_MAX_AREA / viewportArea));
        const factor = Math.max(1, maximumFactor);
        return {
            width: Math.max(1, Math.floor(mapView.width * factor)),
            height: Math.max(1, Math.floor(mapView.height * factor))
        };
    }

    function boundedHeatmapDisplayDimensions(mapView) {
        const viewportArea = Math.max(1, mapView.width * mapView.height);
        const maximumFactor = Math.min(
            HEATMAP_OVERSCAN_FACTOR,
            NETWORK_IMAGE_MAX_DIMENSION / Math.max(1, mapView.width),
            NETWORK_IMAGE_MAX_DIMENSION / Math.max(1, mapView.height),
            Math.sqrt(NETWORK_IMAGE_MAX_AREA / viewportArea));
        const factor = Math.max(1, maximumFactor);
        return {
            width: Math.max(1, Math.floor(mapView.width * factor)),
            height: Math.max(1, Math.floor(mapView.height * factor))
        };
    }

    function boundedHeatmapRasterDimensions(displayWidth, displayHeight) {
        const displayArea = Math.max(1, displayWidth * displayHeight);
        const factor = Math.min(
            HEATMAP_RASTER_SCALE,
            HEATMAP_MAX_DIMENSION / Math.max(1, displayWidth),
            HEATMAP_MAX_DIMENSION / Math.max(1, displayHeight),
            Math.sqrt(HEATMAP_MAX_AREA / displayArea));
        return {
            width: Math.max(1, Math.floor(displayWidth * factor)),
            height: Math.max(1, Math.floor(displayHeight * factor)),
            scale: factor
        };
    }

    function mapViewWorldBounds(mapView, paddingX, paddingY) {
        const transform = worldTransform(mapView);
        const horizontalPadding = Math.max(0, paddingX || 0);
        const verticalPadding = Math.max(0, paddingY || 0);
        return {
            minimumX: state.geometryOriginX
                + (-horizontalPadding - transform.translateX) / transform.scale,
            minimumY: state.geometryOriginY
                + (-verticalPadding - transform.translateY) / transform.scale,
            maximumX: state.geometryOriginX
                + (mapView.width + horizontalPadding - transform.translateX) / transform.scale,
            maximumY: state.geometryOriginY
                + (mapView.height + verticalPadding - transform.translateY) / transform.scale
        };
    }

    function expandedBounds(bounds, amount) {
        return {
            minimumX: bounds.minimumX - amount,
            minimumY: bounds.minimumY - amount,
            maximumX: bounds.maximumX + amount,
            maximumY: bounds.maximumY + amount
        };
    }

    function boundsContain(outerBounds, innerBounds) {
        return Boolean(outerBounds && innerBounds
            && innerBounds.minimumX >= outerBounds.minimumX
            && innerBounds.minimumY >= outerBounds.minimumY
            && innerBounds.maximumX <= outerBounds.maximumX
            && innerBounds.maximumY <= outerBounds.maximumY);
    }

    function imageBoundsCoverMapView(bounds, width, height, mapView) {
        if (!bounds)
            return false;

        const horizontalOverscan = Math.max(0, (width - mapView.width) / 2);
        const verticalOverscan = Math.max(0, (height - mapView.height) / 2);
        const horizontalSafety = Math.min(
            NETWORK_IMAGE_REBUILD_EDGE, horizontalOverscan / 2);
        const verticalSafety = Math.min(
            NETWORK_IMAGE_REBUILD_EDGE, verticalOverscan / 2);
        return boundsContain(bounds, mapViewWorldBounds(
            mapView, horizontalSafety, verticalSafety));
    }

    function heatmapBoundsCoverMapView(bounds, width, height, mapView) {
        if (!bounds)
            return false;

        const horizontalOverscan = Math.max(0, (width - mapView.width) / 2);
        const verticalOverscan = Math.max(0, (height - mapView.height) / 2);
        const horizontalSafety = Math.min(
            HEATMAP_REBUILD_EDGE, horizontalOverscan / 2);
        const verticalSafety = Math.min(
            HEATMAP_REBUILD_EDGE, verticalOverscan / 2);
        return boundsContain(bounds, mapViewWorldBounds(
            mapView, horizontalSafety, verticalSafety));
    }

    function collectionForSpatialQuery(collectionName) {
        if (collectionName === "markers")
            return state.markers;
        if (collectionName === "deviceSegments")
            return state.deviceSegments;
        return state.pipeSegments;
    }

    function indicesInWorldBounds(bounds, collectionName) {
        const minimumCellX = spatialCellCoordinate(bounds.minimumX);
        const maximumCellX = spatialCellCoordinate(bounds.maximumX);
        const minimumCellY = spatialCellCoordinate(bounds.minimumY);
        const maximumCellY = spatialCellCoordinate(bounds.maximumY);
        const cellCount = (maximumCellX - minimumCellX + 1)
            * (maximumCellY - minimumCellY + 1);
        const result = new Set();

        if (cellCount > MAX_SPATIAL_QUERY_CELLS) {
            const collection = collectionForSpatialQuery(collectionName);
            for (let index = 0; index < collection.length; ++index)
                result.add(index);
        } else {
            for (let cellY = minimumCellY; cellY <= maximumCellY; ++cellY) {
                for (let cellX = minimumCellX; cellX <= maximumCellX; ++cellX) {
                    const cell = state.spatialCells.get(spatialCellKey(cellX, cellY));
                    if (!cell)
                        continue;
                    for (const index of cell[collectionName])
                        result.add(index);
                }
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

    function segmentIntersectsBounds(segment, bounds) {
        return Math.max(segment.x1, segment.x2) >= bounds.minimumX
            && Math.min(segment.x1, segment.x2) <= bounds.maximumX
            && Math.max(segment.y1, segment.y2) >= bounds.minimumY
            && Math.min(segment.y1, segment.y2) <= bounds.maximumY;
    }

    function markerIntersectsBounds(marker, bounds, zoom, scale) {
        const markerBounds = markerScreenBounds(marker.entityType, zoom);
        const halfWidth = markerBounds.width / (2 * scale);
        const halfHeight = markerBounds.height / (2 * scale);
        return marker.x + halfWidth >= bounds.minimumX
            && marker.x - halfWidth <= bounds.maximumX
            && marker.y + halfHeight >= bounds.minimumY
            && marker.y - halfHeight <= bounds.maximumY;
    }

    function ownsMapView(mapView) {
        return Boolean(mapView && state.ownerId !== 0
            && mapView.activeOwner === state.ownerId && mapView.topmost);
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

    function isNodeEntityType(entityType) {
        return entityType === ENTITY_JUNCTION || entityType === ENTITY_RESERVOIR
            || entityType === ENTITY_TANK;
    }

    function shouldDisplayHeatmap(mapView) {
        return Boolean(ownsMapView(mapView) && mapView.visible && mapView.ready
            && mapView.initialized && mapView.width > 0 && mapView.height > 0
            && state.geometryReady && state.heatmapVisual !== 0
            && state.heatmapValues.size > 0);
    }

    function ensureHeatmapCanvasElement() {
        if (!state.layer)
            return null;

        if (!state.heatmapCanvas) {
            state.heatmapCanvas = document.createElement("canvas");
            state.heatmapCanvas.setAttribute("aria-hidden", "true");
            state.heatmapCanvas.style.position = "absolute";
            state.heatmapCanvas.style.left = "0";
            state.heatmapCanvas.style.top = "0";
            state.heatmapCanvas.style.display = "none";
            state.heatmapCanvas.style.pointerEvents = "none";
            state.heatmapCanvas.style.zIndex = "1";
            state.heatmapCanvas.style.background = "transparent";
            state.layer.insertBefore(state.heatmapCanvas, state.selectionCanvas || null);
        }
        return state.heatmapCanvas;
    }

    function resetHeatmapWebGlState(removeCanvas) {
        if (state.heatmapGl) {
            if (state.heatmapGlBuffer)
                state.heatmapGl.deleteBuffer(state.heatmapGlBuffer);
            if (state.heatmapGlProgram)
                state.heatmapGl.deleteProgram(state.heatmapGlProgram);
        }
        state.heatmapGl = null;
        state.heatmapGlProgram = null;
        state.heatmapGlBuffer = null;
        state.heatmapGlLocations = null;
        state.heatmapGlVertexCount = 0;
        state.heatmapGlNodeCount = 0;
        state.heatmapUploadedRevision = 0;
        if (removeCanvas && state.heatmapCanvas) {
            state.heatmapCanvas.remove();
            state.heatmapCanvas = null;
        }
        state.heatmapMode = null;
    }

    function compileHeatmapShader(gl, type, source) {
        const shader = gl.createShader(type);
        if (!shader)
            return null;
        gl.shaderSource(shader, source);
        gl.compileShader(shader);
        if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
            console.error("AOWIS heatmap shader compilation failed:", gl.getShaderInfoLog(shader));
            gl.deleteShader(shader);
            return null;
        }
        return shader;
    }

    function createHeatmapWebGlProgram(gl) {
        const vertexShader = compileHeatmapShader(gl, gl.VERTEX_SHADER, `
            precision highp float;
            attribute vec2 a_local;
            attribute vec2 a_corner;
            attribute vec3 a_color;
            uniform vec2 u_translate;
            uniform float u_scale;
            uniform vec2 u_viewport;
            uniform float u_radius;
            varying vec2 v_corner;
            varying vec3 v_color;
            void main() {
                vec2 screen = u_translate + a_local * u_scale + a_corner * u_radius;
                vec2 clip = vec2(screen.x / u_viewport.x * 2.0 - 1.0,
                    1.0 - screen.y / u_viewport.y * 2.0);
                gl_Position = vec4(clip, 0.0, 1.0);
                v_corner = a_corner;
                v_color = a_color;
            }
        `);
        const fragmentShader = compileHeatmapShader(gl, gl.FRAGMENT_SHADER, `
            precision mediump float;
            uniform float u_solid_center;
            varying vec2 v_corner;
            varying vec3 v_color;
            void main() {
                float distance_from_center = length(v_corner);
                if (distance_from_center > 1.0)
                    discard;

                float solid_center = clamp(u_solid_center, 0.0, 0.9);
                float half_opacity = solid_center + (1.0 - solid_center) * 0.4375;
                float alpha;
                if (distance_from_center <= solid_center) {
                    alpha = 1.0;
                } else if (distance_from_center <= half_opacity) {
                    float range = max(0.0001, half_opacity - solid_center);
                    alpha = 1.0 - 0.5 * (distance_from_center - solid_center) / range;
                } else {
                    float range = max(0.0001, 1.0 - half_opacity);
                    alpha = 0.5 * (1.0 - (distance_from_center - half_opacity) / range);
                }
                gl_FragColor = vec4(v_color, clamp(alpha, 0.0, 1.0));
            }
        `);
        if (!vertexShader || !fragmentShader) {
            if (vertexShader)
                gl.deleteShader(vertexShader);
            if (fragmentShader)
                gl.deleteShader(fragmentShader);
            return null;
        }

        const program = gl.createProgram();
        if (!program) {
            gl.deleteShader(vertexShader);
            gl.deleteShader(fragmentShader);
            return null;
        }
        gl.attachShader(program, vertexShader);
        gl.attachShader(program, fragmentShader);
        gl.linkProgram(program);
        gl.deleteShader(vertexShader);
        gl.deleteShader(fragmentShader);
        if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
            console.error("AOWIS heatmap shader linking failed:", gl.getProgramInfoLog(program));
            gl.deleteProgram(program);
            return null;
        }
        return program;
    }

    function initializeHeatmapWebGl(canvas) {
        const gl = canvas.getContext("webgl", {
            alpha: true,
            antialias: false,
            depth: false,
            stencil: false,
            premultipliedAlpha: false,
            preserveDrawingBuffer: false
        });
        if (!gl)
            return false;

        const program = createHeatmapWebGlProgram(gl);
        if (!program) {
            resetHeatmapWebGlState(true);
            return false;
        }
        const buffer = gl.createBuffer();
        if (!buffer) {
            gl.deleteProgram(program);
            resetHeatmapWebGlState(true);
            return false;
        }

        state.heatmapGl = gl;
        state.heatmapGlProgram = program;
        state.heatmapGlBuffer = buffer;
        state.heatmapGlLocations = {
            local: gl.getAttribLocation(program, "a_local"),
            corner: gl.getAttribLocation(program, "a_corner"),
            color: gl.getAttribLocation(program, "a_color"),
            translate: gl.getUniformLocation(program, "u_translate"),
            scale: gl.getUniformLocation(program, "u_scale"),
            viewport: gl.getUniformLocation(program, "u_viewport"),
            radius: gl.getUniformLocation(program, "u_radius"),
            solidCenter: gl.getUniformLocation(program, "u_solid_center")
        };
        state.heatmapMode = "webgl";
        canvas.style.opacity = String(
            Math.max(0, Math.min(100, state.heatmapOpacity)) / 100);
        canvas.style.willChange = "contents";
        gl.disable(gl.DEPTH_TEST);
        gl.disable(gl.CULL_FACE);
        gl.enable(gl.BLEND);
        gl.blendFuncSeparate(
            gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA,
            gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
        gl.clearColor(0, 0, 0, 0);
        return true;
    }

    function ensureHeatmapWebGl(mapView) {
        if (state.heatmapMode === "canvas")
            return false;
        const canvas = ensureHeatmapCanvasElement();
        if (!canvas)
            return false;

        if (state.heatmapMode !== "webgl" && !initializeHeatmapWebGl(canvas)) {
            if (!state.heatmapWebGlFailureLogged) {
                console.warn("AOWIS WebGL heatmap unavailable; falling back to Canvas 2D");
                state.heatmapWebGlFailureLogged = true;
            }
            if (!state.heatmapCanvas)
                ensureHeatmapCanvasElement();
            state.heatmapMode = "canvas";
            return false;
        }

        const pixelRatio = heatmapWebGlPixelRatio(mapView);
        const rasterWidth = Math.max(1, Math.ceil(mapView.width * pixelRatio));
        const rasterHeight = Math.max(1, Math.ceil(mapView.height * pixelRatio));
        if (canvas.width !== rasterWidth)
            canvas.width = rasterWidth;
        if (canvas.height !== rasterHeight)
            canvas.height = rasterHeight;
        const cssWidth = `${Math.max(1, mapView.width)}px`;
        const cssHeight = `${Math.max(1, mapView.height)}px`;
        if (canvas.style.width !== cssWidth)
            canvas.style.width = cssWidth;
        if (canvas.style.height !== cssHeight)
            canvas.style.height = cssHeight;
        return true;
    }

    function rebuildHeatmapWebGlBuffer() {
        const gl = state.heatmapGl;
        if (!gl || !state.heatmapGlBuffer)
            return false;
        if (state.heatmapUploadedRevision === state.heatmapDataRevision)
            return true;

        const corners = [
            -1, -1, 1, -1, -1, 1,
            -1, 1, 1, -1, 1, 1
        ];
        let heatmapNodeCount = 0;
        for (const marker of state.markers) {
            if (isNodeEntityType(marker.entityType) && state.heatmapValues.has(marker.renderId))
                ++heatmapNodeCount;
        }

        const floatsPerVertex = 7;
        const verticesPerNode = 6;
        const data = new Float32Array(heatmapNodeCount * verticesPerNode * floatsPerVertex);
        let offset = 0;
        for (const marker of state.markers) {
            if (!isNodeEntityType(marker.entityType) || !state.heatmapValues.has(marker.renderId))
                continue;
            const fraction = heatmapValueFraction(state.heatmapValues.get(marker.renderId));
            if (fraction === null)
                continue;
            const color = rampRgb(fraction);
            const localX = marker.x - state.geometryOriginX;
            const localY = marker.y - state.geometryOriginY;
            for (let cornerIndex = 0; cornerIndex < corners.length; cornerIndex += 2) {
                data[offset++] = localX;
                data[offset++] = localY;
                data[offset++] = corners[cornerIndex];
                data[offset++] = corners[cornerIndex + 1];
                data[offset++] = color.red / 255;
                data[offset++] = color.green / 255;
                data[offset++] = color.blue / 255;
            }
        }

        gl.bindBuffer(gl.ARRAY_BUFFER, state.heatmapGlBuffer);
        gl.bufferData(gl.ARRAY_BUFFER, offset === data.length ? data : data.subarray(0, offset), gl.STATIC_DRAW);
        state.heatmapGlVertexCount = offset / floatsPerVertex;
        state.heatmapGlNodeCount = state.heatmapGlVertexCount / verticesPerNode;
        state.heatmapUploadedRevision = state.heatmapDataRevision;
        return true;
    }

    function heatmapWebGlNodeCount() {
        if (state.heatmapUploadedRevision === state.heatmapDataRevision
            && state.heatmapGlNodeCount > 0) {
            return state.heatmapGlNodeCount;
        }

        let count = 0;
        for (const marker of state.markers) {
            if (isNodeEntityType(marker.entityType) && state.heatmapValues.has(marker.renderId))
                ++count;
        }
        return count;
    }

    function heatmapWebGlPixelRatio(mapView) {
        const cssRadius = Math.max(1, heatmapRadiusWorldPixels() * scaleForZoom(mapView.zoom));
        const nodeCount = Math.max(1, heatmapWebGlNodeCount());
        const viewportArea = Math.max(1, mapView.width * mapView.height);
        const circleArea = Math.PI * cssRadius * cssRadius;
        const estimatedCssFragmentsPerNode = Math.min(viewportArea, circleArea);
        const budgetRatio = Math.sqrt(
            HEATMAP_WEBGL_TARGET_FRAGMENT_BUDGET
                / Math.max(1, nodeCount * estimatedCssFragmentsPerNode));
        const radiusRatio = HEATMAP_WEBGL_TARGET_RADIUS_PIXELS / cssRadius;
        const ratio = Math.min(HEATMAP_WEBGL_MAX_PIXEL_RATIO, radiusRatio, budgetRatio);
        return Math.max(HEATMAP_WEBGL_MIN_PIXEL_RATIO, ratio);
    }

    function renderHeatmapWebGl(mapView) {
        if (!ensureHeatmapWebGl(mapView) || !state.heatmapGl || !state.heatmapGlProgram
            || !state.heatmapGlLocations || !rebuildHeatmapWebGlBuffer()) {
            return false;
        }

        const gl = state.heatmapGl;
        if (typeof gl.isContextLost === "function" && gl.isContextLost()) {
            resetHeatmapWebGlState(true);
            state.heatmapMode = "canvas";
            return false;
        }
        const locations = state.heatmapGlLocations;
        const canvas = state.heatmapCanvas;
        const transform = worldTransform(mapView);
        const radius = Math.max(1, heatmapRadiusWorldPixels() * transform.scale);
        const stride = 7 * Float32Array.BYTES_PER_ELEMENT;

        gl.viewport(0, 0, canvas.width, canvas.height);
        gl.clear(gl.COLOR_BUFFER_BIT);
        gl.useProgram(state.heatmapGlProgram);
        gl.bindBuffer(gl.ARRAY_BUFFER, state.heatmapGlBuffer);
        gl.enableVertexAttribArray(locations.local);
        gl.vertexAttribPointer(locations.local, 2, gl.FLOAT, false, stride, 0);
        gl.enableVertexAttribArray(locations.corner);
        gl.vertexAttribPointer(locations.corner, 2, gl.FLOAT, false, stride, 2 * Float32Array.BYTES_PER_ELEMENT);
        gl.enableVertexAttribArray(locations.color);
        gl.vertexAttribPointer(locations.color, 3, gl.FLOAT, false, stride, 4 * Float32Array.BYTES_PER_ELEMENT);
        gl.uniform2f(locations.translate, transform.translateX, transform.translateY);
        gl.uniform1f(locations.scale, transform.scale);
        gl.uniform2f(locations.viewport, Math.max(1, mapView.width), Math.max(1, mapView.height));
        gl.uniform1f(locations.radius, radius);
        gl.uniform1f(locations.solidCenter, Math.max(0, Math.min(0.9, state.heatmapSolidCenterPercent / 100)));
        if (state.heatmapGlVertexCount > 0)
            gl.drawArrays(gl.TRIANGLES, 0, state.heatmapGlVertexCount);
        if (canvas.style.display !== "block")
            canvas.style.display = "block";
        return true;
    }

    function applyHeatmapOpacity() {
        if (!state.heatmapCanvas)
            return;
        state.heatmapCanvas.style.opacity = String(
            Math.max(0, Math.min(100, state.heatmapOpacity)) / 100);
    }

    function heatmapSpecification(mapView) {
        const zoom = mapView.zoom;
        const scale = scaleForZoom(zoom);
        const displayDimensions = boundedHeatmapDisplayDimensions(mapView);
        const rasterDimensions = boundedHeatmapRasterDimensions(
            displayDimensions.width, displayDimensions.height);
        const transform = worldTransform(mapView);
        const centerX = state.geometryOriginX
            + (mapView.width / 2 - transform.translateX) / scale;
        const centerY = state.geometryOriginY
            + (mapView.height / 2 - transform.translateY) / scale;
        const bounds = {
            minimumX: centerX - displayDimensions.width / (2 * scale),
            minimumY: centerY - displayDimensions.height / (2 * scale),
            maximumX: centerX + displayDimensions.width / (2 * scale),
            maximumY: centerY + displayDimensions.height / (2 * scale)
        };
        return {
            zoom: zoom,
            scale: scale,
            bounds: bounds,
            displayWidth: displayDimensions.width,
            displayHeight: displayDimensions.height,
            rasterWidth: rasterDimensions.width,
            rasterHeight: rasterDimensions.height,
            rasterScale: rasterDimensions.scale,
            offsetX: (bounds.minimumX - state.geometryOriginX) * scale,
            offsetY: (bounds.minimumY - state.geometryOriginY) * scale
        };
    }

    function heatmapValueFraction(value) {
        if (!Number.isFinite(value) || !Number.isFinite(state.heatmapMinimum)
            || !Number.isFinite(state.heatmapMaximum)) {
            return null;
        }
        if (state.heatmapMinimum === state.heatmapMaximum)
            return 0.5;
        return Math.max(0, Math.min(1,
            (value - state.heatmapMinimum)
                / (state.heatmapMaximum - state.heatmapMinimum)));
    }

    function heatmapRadiusWorldPixels() {
        const centerY = (state.geometryMinimumY + state.geometryMaximumY) / 2;
        const centerLatitude = worldPixelToLatitude(centerY);
        const metersPerPixel = Math.max(0.000001, metersPerReferencePixel(centerLatitude));
        return Math.max(1, state.heatmapRadiusMeters / metersPerPixel);
    }

    function heatmapKernelRadius(radius) {
        return Math.max(1, Math.min(HEATMAP_MAX_KERNEL_RADIUS, radius));
    }

    function heatmapColorBucketCount(radius) {
        const kernelRadius = heatmapKernelRadius(radius);
        const diameter = Math.max(3, Math.ceil(kernelRadius * 2) + 2);
        const maximumByMemory = Math.floor(
            HEATMAP_MAX_KERNEL_CACHE_PIXELS / (diameter * diameter));
        return Math.max(RAMP_COLORS.length, Math.min(
            HEATMAP_MAX_COLOR_BUCKETS, maximumByMemory));
    }

    function createHeatmapKernel(radius, color) {
        const boundedRadius = Math.max(1, radius);
        const kernelRadius = heatmapKernelRadius(boundedRadius);
        const diameter = Math.max(3, Math.ceil(kernelRadius * 2) + 2);
        const center = diameter / 2;
        const canvas = document.createElement("canvas");
        canvas.width = diameter;
        canvas.height = diameter;
        const context = canvas.getContext("2d");
        if (!context)
            return null;

        const red = Math.round(color.red);
        const green = Math.round(color.green);
        const blue = Math.round(color.blue);
        const solidCenterFraction = Math.max(0, Math.min(0.9,
            state.heatmapSolidCenterPercent / 100));
        const halfOpacityFraction = solidCenterFraction
            + (1 - solidCenterFraction) * 0.4375;
        const gradient = context.createRadialGradient(
            center, center, 0, center, center, kernelRadius);
        gradient.addColorStop(0, `rgba(${red}, ${green}, ${blue}, 1)`);
        gradient.addColorStop(solidCenterFraction,
            `rgba(${red}, ${green}, ${blue}, 1)`);
        gradient.addColorStop(halfOpacityFraction,
            `rgba(${red}, ${green}, ${blue}, 0.5)`);
        gradient.addColorStop(1, `rgba(${red}, ${green}, ${blue}, 0)`);
        context.fillStyle = gradient;
        context.fillRect(0, 0, diameter, diameter);
        return {
            canvas: canvas,
            diameter: Math.max(3, Math.ceil(boundedRadius * 2) + 2)
        };
    }

    function heatmapKernel(radius, colorBucketCount, bucket) {
        const roundedRadius = Math.max(1, Math.round(radius * 4) / 4);
        const bucketFraction = colorBucketCount <= 1
            ? 0.5 : bucket / (colorBucketCount - 1);
        const color = rampRgb(bucketFraction);
        const key = `${roundedRadius}:${colorBucketCount}:${bucket}:${state.heatmapSolidCenterPercent}`;
        let kernel = state.heatmapKernelCache.get(key);
        if (kernel)
            return kernel;

        kernel = createHeatmapKernel(roundedRadius, color);
        if (!kernel)
            return null;
        if (state.heatmapKernelCache.size >= HEATMAP_MAX_COLOR_BUCKETS * 4)
            state.heatmapKernelCache.clear();
        state.heatmapKernelCache.set(key, kernel);
        return kernel;
    }

    function renderHeatmap(specification) {
        const canvas = document.createElement("canvas");
        canvas.width = specification.rasterWidth;
        canvas.height = specification.rasterHeight;
        const context = canvas.getContext("2d");
        if (!context)
            return null;

        const radiusWorldPixels = heatmapRadiusWorldPixels();
        const radius = Math.max(
            1, radiusWorldPixels * specification.scale * specification.rasterScale);
        const colorBucketCount = heatmapColorBucketCount(radius);
        const queryPadding = radiusWorldPixels;
        const queryBounds = expandedBounds(specification.bounds, queryPadding);
        context.globalCompositeOperation = "source-over";
        for (const index of indicesInWorldBounds(queryBounds, "markers")) {
            const marker = state.markers[index];
            if (!isNodeEntityType(marker.entityType)
                || !state.heatmapValues.has(marker.renderId)) {
                continue;
            }

            const fraction = heatmapValueFraction(
                state.heatmapValues.get(marker.renderId));
            if (fraction === null)
                continue;

            const x = (marker.x - specification.bounds.minimumX)
                * specification.scale * specification.rasterScale;
            const y = (marker.y - specification.bounds.minimumY)
                * specification.scale * specification.rasterScale;
            if (x + radius < 0 || y + radius < 0
                || x - radius > specification.rasterWidth
                || y - radius > specification.rasterHeight) {
                continue;
            }

            const bucket = Math.round(fraction * (colorBucketCount - 1));
            const kernel = heatmapKernel(radius, colorBucketCount, bucket);
            if (!kernel)
                return null;
            context.drawImage(
                kernel.canvas,
                x - kernel.diameter / 2,
                y - kernel.diameter / 2,
                kernel.diameter,
                kernel.diameter);
        }
        return canvas;
    }

    function ensureCanvasHeatmapPresentationCanvas(mapView) {
        if (!state.layer || state.heatmapMode === "webgl")
            return false;

        const canvas = ensureHeatmapCanvasElement();
        if (!canvas)
            return false;
        state.heatmapMode = "canvas";
        canvas.style.imageRendering = "auto";
        canvas.style.opacity = String(
            Math.max(0, Math.min(100, state.heatmapOpacity)) / 100);

        const dimensions = boundedHeatmapRasterDimensions(mapView.width, mapView.height);
        if (state.heatmapCanvas.width !== dimensions.width
            || state.heatmapCanvas.height !== dimensions.height) {
            state.heatmapCanvas.width = dimensions.width;
            state.heatmapCanvas.height = dimensions.height;
        }
        const cssWidth = `${Math.max(1, mapView.width)}px`;
        const cssHeight = `${Math.max(1, mapView.height)}px`;
        if (state.heatmapCanvas.style.width !== cssWidth)
            state.heatmapCanvas.style.width = cssWidth;
        if (state.heatmapCanvas.style.height !== cssHeight)
            state.heatmapCanvas.style.height = cssHeight;
        return true;
    }

    function presentCanvasHeatmap(mapView) {
        state.heatmapPresentationFrameRequest = 0;
        if (!state.heatmapCacheCanvas || state.heatmapZoom !== mapView.zoom
            || !shouldDisplayHeatmap(mapView)
            || !ensureCanvasHeatmapPresentationCanvas(mapView)) {
            if (state.heatmapCanvas)
                state.heatmapCanvas.style.display = "none";
            return;
        }

        const context = state.heatmapCanvas.getContext("2d");
        if (!context)
            return;

        const destinationScaleX = state.heatmapCanvas.width / Math.max(1, mapView.width);
        const destinationScaleY = state.heatmapCanvas.height / Math.max(1, mapView.height);
        const transform = worldTransform(mapView);
        const cacheLeft = transform.translateX + state.heatmapOffsetX;
        const cacheTop = transform.translateY + state.heatmapOffsetY;
        const sourceCssX = Math.max(0, -cacheLeft);
        const sourceCssY = Math.max(0, -cacheTop);
        const destinationCssX = Math.max(0, cacheLeft);
        const destinationCssY = Math.max(0, cacheTop);
        const visibleCssWidth = Math.max(0, Math.min(
            mapView.width - destinationCssX, state.heatmapWidth - sourceCssX));
        const visibleCssHeight = Math.max(0, Math.min(
            mapView.height - destinationCssY, state.heatmapHeight - sourceCssY));

        context.clearRect(0, 0, state.heatmapCanvas.width, state.heatmapCanvas.height);
        if (visibleCssWidth > 0 && visibleCssHeight > 0) {
            context.drawImage(
                state.heatmapCacheCanvas,
                sourceCssX * state.heatmapRasterScale,
                sourceCssY * state.heatmapRasterScale,
                visibleCssWidth * state.heatmapRasterScale,
                visibleCssHeight * state.heatmapRasterScale,
                destinationCssX * destinationScaleX,
                destinationCssY * destinationScaleY,
                visibleCssWidth * destinationScaleX,
                visibleCssHeight * destinationScaleY);
        }
        if (state.heatmapCanvas.style.display !== "block")
            state.heatmapCanvas.style.display = "block";
    }

    function positionCanvasHeatmap(mapView) {
        if (!state.heatmapCacheCanvas || state.heatmapZoom !== mapView.zoom) {
            if (state.heatmapCanvas)
                state.heatmapCanvas.style.display = "none";
            return;
        }
        if (state.heatmapPresentationFrameRequest !== 0)
            return;
        state.heatmapPresentationFrameRequest = window.requestAnimationFrame(() => {
            const latestMapView = state.lastMapView;
            if (latestMapView)
                presentCanvasHeatmap(latestMapView);
            else
                state.heatmapPresentationFrameRequest = 0;
        });
    }

    function renderedCanvasHeatmapCoversMapView(mapView) {
        return Boolean(state.heatmapCacheCanvas && state.heatmapZoom === mapView.zoom
            && heatmapBoundsCoverMapView(
                state.heatmapBounds, state.heatmapWidth, state.heatmapHeight, mapView));
    }

    function requestCanvasHeatmap(mapView) {
        if (!state.layer || !shouldDisplayHeatmap(mapView)
            || renderedCanvasHeatmapCoversMapView(mapView)) {
            return;
        }

        const specification = heatmapSpecification(mapView);
        const canvas = renderHeatmap(specification);
        if (!canvas)
            return;

        state.heatmapCacheCanvas = canvas;
        state.heatmapZoom = specification.zoom;
        state.heatmapOffsetX = specification.offsetX;
        state.heatmapOffsetY = specification.offsetY;
        state.heatmapWidth = specification.displayWidth;
        state.heatmapHeight = specification.displayHeight;
        state.heatmapRasterScale = specification.rasterScale;
        state.heatmapBounds = specification.bounds;
        positionCanvasHeatmap(mapView);
    }

    function scheduleCanvasHeatmap() {
        if (state.heatmapFrameRequest !== 0)
            return;

        state.heatmapFrameRequest = window.requestAnimationFrame(() => {
            state.heatmapFrameRequest = 0;
            const mapView = state.lastMapView;
            if (!mapView || !shouldDisplayHeatmap(mapView))
                return;
            requestCanvasHeatmap(mapView);
        });
    }

    function scheduleHeatmap() {
        if (state.heatmapFrameRequest !== 0)
            return;

        state.heatmapFrameRequest = window.requestAnimationFrame(() => {
            state.heatmapFrameRequest = 0;
            const mapView = state.lastMapView;
            if (!mapView || !shouldDisplayHeatmap(mapView)) {
                if (state.heatmapCanvas)
                    state.heatmapCanvas.style.display = "none";
                return;
            }

            if (renderHeatmapWebGl(mapView))
                return;

            if (state.heatmapCacheCanvas && state.heatmapZoom === mapView.zoom)
                positionCanvasHeatmap(mapView);
            else if (state.heatmapCanvas)
                state.heatmapCanvas.style.display = "none";
            if (!renderedCanvasHeatmapCoversMapView(mapView))
                scheduleCanvasHeatmap();
        });
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
        if (cellCount > MAX_SPATIAL_QUERY_CELLS) {
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

    function polylineMidpoint(vertices) {
        let totalLength = 0;
        const segmentLengths = [];
        for (let index = 1; index < vertices.length; ++index) {
            const deltaX = vertices[index].x - vertices[index - 1].x;
            const deltaY = vertices[index].y - vertices[index - 1].y;
            const length = Math.hypot(deltaX, deltaY);
            segmentLengths.push(length);
            totalLength += length;
        }

        if (totalLength <= 0)
            return vertices[0];

        const midpointDistance = totalLength / 2;
        let traversed = 0;
        for (let index = 0; index < segmentLengths.length; ++index) {
            const segmentLength = segmentLengths[index];
            if (traversed + segmentLength < midpointDistance) {
                traversed += segmentLength;
                continue;
            }

            const ratio = segmentLength > 0
                ? (midpointDistance - traversed) / segmentLength : 0;
            return {
                x: vertices[index].x + (vertices[index + 1].x - vertices[index].x) * ratio,
                y: vertices[index].y + (vertices[index + 1].y - vertices[index].y) * ratio
            };
        }
        return vertices[vertices.length - 1];
    }

    function buildGeometry(snapshot) {
        resetNetworkImage(true);
        clearHeatmap();
        const nodesByRenderId = new Map();
        let anchorX = null;
        let minimumX = Number.POSITIVE_INFINITY;
        let minimumY = Number.POSITIVE_INFINITY;
        let maximumX = Number.NEGATIVE_INFINITY;
        let maximumY = Number.NEGATIVE_INFINITY;

        state.markers = [];
        state.deviceSegments = [];
        state.pipeSegments = [];
        state.entityMarkers.clear();
        state.entitySegments.clear();
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
            nodesByRenderId.set(projectedNode.renderId, projectedNode);
            state.markers.push(projectedNode);
            state.entityMarkers.set(`${projectedNode.entityType}:${projectedNode.renderId}`, projectedNode);
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
            const segmentCollection = projectedLink.entityType === ENTITY_PIPE
                ? state.pipeSegments : state.deviceSegments;
            for (let index = 1; index < vertices.length; ++index) {
                const segment = {
                    renderId: projectedLink.renderId,
                    entityType: projectedLink.entityType,
                    x1: vertices[index - 1].x,
                    y1: vertices[index - 1].y,
                    x2: vertices[index].x,
                    y2: vertices[index].y
                };
                segmentCollection.push(segment);
                const entityKey = `${projectedLink.entityType}:${projectedLink.renderId}`;
                const entitySegments = state.entitySegments.get(entityKey) || [];
                entitySegments.push(segment);
                state.entitySegments.set(entityKey, entitySegments);
            }

            if (projectedLink.entityType === ENTITY_PUMP || projectedLink.entityType === ENTITY_VALVE) {
                const center = polylineMidpoint(vertices);
                const deviceMarker = {
                    renderId: projectedLink.renderId,
                    entityType: projectedLink.entityType,
                    x: center.x,
                    y: center.y
                };
                state.markers.push(deviceMarker);
                state.entityMarkers.set(
                    `${projectedLink.entityType}:${projectedLink.renderId}`, deviceMarker);
            }
        }

        if (!Number.isFinite(minimumX) || !Number.isFinite(minimumY)) {
            state.geometryOriginX = 0;
            state.geometryOriginY = 0;
            state.geometryMinimumX = 0;
            state.geometryMinimumY = 0;
            state.geometryMaximumX = 0;
            state.geometryMaximumY = 0;
            state.geometryReady = false;
            return;
        }

        state.geometryOriginX = (minimumX + maximumX) / 2;
        state.geometryOriginY = (minimumY + maximumY) / 2;
        state.geometryMinimumX = minimumX;
        state.geometryMinimumY = minimumY;
        state.geometryMaximumX = maximumX;
        state.geometryMaximumY = maximumY;

        state.geometryReady = state.markers.length > 0
            || state.pipeSegments.length > 0 || state.deviceSegments.length > 0;
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

    function clearSelectionCanvas() {
        if (!state.selectionCanvas)
            return;
        const context = state.selectionCanvas.getContext("2d");
        if (context)
            context.clearRect(0, 0, state.selectionCanvas.width, state.selectionCanvas.height);
        state.selectionCanvas.style.display = "none";
    }

    function renderSelection() {
        state.selectionRenderPending = false;
        const mapView = state.lastMapView;
        if (!state.selectionCanvas || !shouldDisplayNetwork(mapView)
            || state.selectedRenderId === 0 || state.selectedEntityType === 0) {
            clearSelectionCanvas();
            return;
        }

        const context = vectorRenderer().configureCanvas(
            state.selectionCanvas,
            mapView.width,
            mapView.height,
            Math.min(devicePixelRatio(), NETWORK_STATIC_MAX_PIXEL_RATIO));
        if (!context)
            return;

        const transform = worldTransform(mapView);
        const key = `${state.selectedEntityType}:${state.selectedRenderId}`;
        const segments = state.entitySegments.get(key) || [];
        if (segments.length > 0) {
            context.beginPath();
            for (const segment of segments) {
                context.moveTo(
                    transform.translateX + (segment.x1 - state.geometryOriginX) * transform.scale,
                    transform.translateY + (segment.y1 - state.geometryOriginY) * transform.scale);
                context.lineTo(
                    transform.translateX + (segment.x2 - state.geometryOriginX) * transform.scale,
                    transform.translateY + (segment.y2 - state.geometryOriginY) * transform.scale);
            }
            context.strokeStyle = SELECTED_COLOR;
            context.lineWidth = Math.max(3, state.linkThicknessPixels + 2);
            context.lineCap = "round";
            context.lineJoin = "round";
            context.stroke();
        }

        const marker = state.entityMarkers.get(key);
        if (marker) {
            const centerX = transform.translateX
                + (marker.x - state.geometryOriginX) * transform.scale;
            const centerY = transform.translateY
                + (marker.y - state.geometryOriginY) * transform.scale;
            if (marker.entityType === ENTITY_JUNCTION) {
                context.beginPath();
                context.arc(
                    centerX, centerY,
                    junctionDotDiameterForZoom(mapView.zoom) / 2 + 2,
                    0, Math.PI * 2);
                context.fillStyle = SELECTED_COLOR;
                context.fill();
            } else {
                const bounds = markerScreenBounds(marker.entityType, mapView.zoom);
                const image = tintedIcon(
                    marker.entityType, bounds.width, bounds.height, SELECTED_COLOR);
                if (image) {
                    context.drawImage(
                        image,
                        centerX - bounds.width / 2,
                        centerY - bounds.height / 2,
                        bounds.width,
                        bounds.height);
                }
            }
        }
        state.selectionCanvas.style.display = "block";
    }

    function scheduleSelectionRender() {
        if (!state.selectionCanvas || state.selectionRenderPending)
            return;
        if (state.selectedRenderId === 0 && state.selectionCanvas.style.display !== "block")
            return;
        state.selectionRenderPending = true;
        window.requestAnimationFrame(renderSelection);
    }

    function setSelectedEntity(renderId, entityType) {
        const nextRenderId = Number(renderId) >>> 0;
        const nextEntityType = nextRenderId === 0 ? 0 : Number(entityType) | 0;
        if (state.selectedRenderId === nextRenderId
            && state.selectedEntityType === nextEntityType) {
            return;
        }
        state.selectedRenderId = nextRenderId;
        state.selectedEntityType = nextEntityType;
        scheduleSelectionRender();
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

        if (!state.geometryReady) {
            if (state.networkImage)
                state.networkImage.style.display = "none";
            if (state.heatmapCanvas)
                state.heatmapCanvas.style.display = "none";
            clearSelectionCanvas();
            return;
        }

        if (shouldDisplayHeatmap(mapView))
            scheduleHeatmap();
        else if (state.heatmapCanvas)
            state.heatmapCanvas.style.display = "none";

        positionNetworkImage(mapView);
        requestNetworkImage(mapView.zoom);
        scheduleSelectionRender();
    }

    function initialize() {
        if (!window.aowisBrowserMap || typeof window.aowisBrowserMap.subscribeView !== "function")
            throw new Error("AOWIS browser network requires aowis-browser-map.js");
        vectorRenderer();

        state.unsubscribeView = window.aowisBrowserMap.subscribeView(handleMapViewChanged);
        state.pointerMoveHandler = handlePointerMove;
        state.pointerLeaveHandler = clearHoverCursor;
        window.addEventListener("pointermove", state.pointerMoveHandler, { capture: true, passive: true });
        window.addEventListener("pointerleave", state.pointerLeaveHandler, { capture: true, passive: true });
        window.addEventListener("blur", state.pointerLeaveHandler, { passive: true });
        loadIconImages();
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

    function nearestMarkerHit(pointX, pointY, scale, zoom) {
        const worldTolerance = maximumMarkerSizeForZoom(zoom) / (2 * scale);
        const candidates = candidateIndices(pointX, pointY, worldTolerance, "markers");
        let bestHit = null;
        let bestDistanceSquared = Number.POSITIVE_INFINITY;

        for (const index of candidates) {
            const marker = state.markers[index];
            const deltaXScreen = (pointX - marker.x) * scale;
            const deltaYScreen = (pointY - marker.y) * scale;
            const bounds = markerScreenBounds(marker.entityType, zoom);
            const halfWidth = bounds.width / 2;
            const halfHeight = bounds.height / 2;
            if (Math.abs(deltaXScreen) > halfWidth || Math.abs(deltaYScreen) > halfHeight)
                continue;

            if (bounds.hitShape === "ellipse") {
                const normalizedX = deltaXScreen / halfWidth;
                const normalizedY = deltaYScreen / halfHeight;
                if (normalizedX * normalizedX + normalizedY * normalizedY > 1)
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
        const worldTolerance = linkHitDistance() / scale;
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
        const markerHit = nearestMarkerHit(pointX, pointY, transform.scale, mapView.zoom);
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
        buildGeometry(snapshot);
        if (state.lastMapView)
            handleMapViewChanged(state.lastMapView);
    }

    function symbologyValues(entries) {
        const values = new Map();
        if (!Array.isArray(entries))
            return values;

        for (const entry of entries) {
            if (!Array.isArray(entry) || entry.length < 2)
                continue;
            const renderId = Number(entry[0]);
            const value = Number(entry[1]);
            if (Number.isFinite(renderId) && renderId > 0 && Number.isFinite(value))
                values.set(renderId, value);
        }
        return values;
    }

    function symbologyValuesEqual(first, second) {
        if (first.size !== second.size)
            return false;
        for (const [renderId, value] of first) {
            if (!second.has(renderId) || second.get(renderId) !== value)
                return false;
        }
        return true;
    }

    function setSymbology(symbology) {
        if (!symbology)
            throw new TypeError("Invalid AOWIS browser network symbology");

        const nodeVisual = Number(symbology.nodeVisual) | 0;
        const nodeSizePercent = Math.max(
            50, Math.min(250, Number(symbology.nodeSizePercent) || 100));
        const iconSizePercent = Math.max(
            50, Math.min(250, Number(symbology.iconSizePercent) || 100));
        const nodeMinimum = Number(symbology.nodeMinimum);
        const nodeMaximum = Number(symbology.nodeMaximum);
        const nodeValues = symbologyValues(symbology.nodeValues);
        const linkVisual = Number(symbology.linkVisual) | 0;
        const linkThicknessPixels = Math.max(
            1, Math.min(12, Number(symbology.linkThicknessPixels) || 3));
        const linkMinimum = Number(symbology.linkMinimum);
        const linkMaximum = Number(symbology.linkMaximum);
        const linkValues = symbologyValues(symbology.linkValues);
        const heatmapVisual = Number(symbology.heatmapVisual) | 0;
        const heatmapMinimum = Number(symbology.heatmapMinimum);
        const heatmapMaximum = Number(symbology.heatmapMaximum);
        const heatmapValues = symbologyValues(symbology.heatmapValues);
        const heatmapOpacity = Math.max(
            0, Math.min(100, Number(symbology.heatmapOpacity) || 0));
        const heatmapRadiusMeters = Math.max(
            10, Math.min(1000, Number(symbology.heatmapRadiusMeters) || 400));
        const heatmapSolidCenterPercent = Math.max(0, Math.min(100,
            Number(symbology.heatmapSolidCenterPercent) || 0));

        const nodeSymbologyChanged = state.nodeVisual !== nodeVisual
            || state.nodeMinimum !== nodeMinimum
            || state.nodeMaximum !== nodeMaximum
            || !symbologyValuesEqual(state.nodeValues, nodeValues);
        const linkSymbologyChanged = state.linkVisual !== linkVisual
            || state.linkMinimum !== linkMinimum
            || state.linkMaximum !== linkMaximum
            || !symbologyValuesEqual(state.linkValues, linkValues);
        const networkChanged = nodeSymbologyChanged
            || linkSymbologyChanged
            || state.nodeSizePercent !== nodeSizePercent
            || state.iconSizePercent !== iconSizePercent
            || state.linkThicknessPixels !== linkThicknessPixels;
        const heatmapChanged = state.heatmapVisual !== heatmapVisual
            || state.heatmapMinimum !== heatmapMinimum
            || state.heatmapMaximum !== heatmapMaximum
            || state.heatmapRadiusMeters !== heatmapRadiusMeters
            || state.heatmapSolidCenterPercent !== heatmapSolidCenterPercent
            || !symbologyValuesEqual(state.heatmapValues, heatmapValues);
        const heatmapOpacityChanged = state.heatmapOpacity !== heatmapOpacity;

        state.nodeVisual = nodeVisual;
        state.nodeSizePercent = nodeSizePercent;
        state.iconSizePercent = iconSizePercent;
        state.nodeMinimum = nodeMinimum;
        state.nodeMaximum = nodeMaximum;
        state.nodeValues = nodeValues;
        if (nodeSymbologyChanged)
            state.nodeColors = buildSymbologyColorMap(nodeVisual, nodeValues, nodeMinimum, nodeMaximum);
        state.linkVisual = linkVisual;
        state.linkThicknessPixels = linkThicknessPixels;
        state.linkMinimum = linkMinimum;
        state.linkMaximum = linkMaximum;
        state.linkValues = linkValues;
        if (linkSymbologyChanged)
            state.linkColors = buildSymbologyColorMap(linkVisual, linkValues, linkMinimum, linkMaximum);
        state.heatmapVisual = heatmapVisual;
        state.heatmapMinimum = heatmapMinimum;
        state.heatmapMaximum = heatmapMaximum;
        state.heatmapValues = heatmapValues;
        state.heatmapOpacity = heatmapOpacity;
        state.heatmapRadiusMeters = heatmapRadiusMeters;
        state.heatmapSolidCenterPercent = heatmapSolidCenterPercent;

        if (networkChanged)
            invalidateNetworkCache();
        if (heatmapChanged)
            clearHeatmap();
        else if (heatmapOpacityChanged)
            applyHeatmapOpacity();
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

        resetNetworkImage(true);
        clearHeatmap();
        resetHeatmapWebGlState(false);
        if (state.heatmapCanvas)
            state.heatmapCanvas.remove();
        state.heatmapCanvas = null;
        state.heatmapMode = null;
        if (state.selectionCanvas)
            state.selectionCanvas.remove();
        if (state.layer)
            state.layer.remove();
        state.layer = null;
        state.networkImage = null;
        state.networkImageObjectUrl = null;
        state.pendingNetworkImage = null;
        state.pendingNetworkImageObjectUrl = null;
        state.selectionCanvas = null;
        state.selectionRenderPending = false;
        state.selectedRenderId = 0;
        state.selectedEntityType = 0;
        state.entityMarkers.clear();
        state.entitySegments.clear();
        state.geometryOriginX = 0;
        state.geometryOriginY = 0;
        state.geometryMinimumX = 0;
        state.geometryMinimumY = 0;
        state.geometryMaximumX = 0;
        state.geometryMaximumY = 0;
        state.iconImages.clear();
        state.iconSvgTexts.clear();
        state.coloredIconSvgDataUrls.clear();
        state.tintedIconCache.clear();
        state.geometryReady = false;
        state.width = 0;
        state.height = 0;
        state.lastMapView = null;
        state.markers = [];
        state.deviceSegments = [];
        state.pipeSegments = [];
        state.globalDeviceSegments = [];
        state.globalPipeSegments = [];
        state.spatialCells.clear();
        state.nodeVisual = 0;
        state.nodeSizePercent = 100;
        state.iconSizePercent = 100;
        state.nodeMinimum = 0;
        state.nodeMaximum = 0;
        state.nodeValues = new Map();
        state.nodeColors = new Map();
        state.linkVisual = 0;
        state.linkThicknessPixels = 3;
        state.linkMinimum = 0;
        state.linkMaximum = 0;
        state.linkValues = new Map();
        state.linkColors = new Map();
        state.heatmapVisual = 0;
        state.heatmapMinimum = 0;
        state.heatmapMaximum = 0;
        state.heatmapValues = new Map();
        state.heatmapOpacity = 75;
        state.heatmapRadiusMeters = 400;
        state.heatmapSolidCenterPercent = 70;
        state.heatmapGlVertexCount = 0;
        state.heatmapGlNodeCount = 0;
        state.heatmapDataRevision = 1;
        state.heatmapUploadedRevision = 0;
    }

    window.aowisBrowserNetwork = {
        setSnapshot: setSnapshot,
        setSymbology: setSymbology,
        setBackground: setBackground,
        setOwnerId: setOwnerId,
        setSelectedEntity: setSelectedEntity,
        hitTest: hitTest,
        destroy: destroy
    };

    initialize();
})();
