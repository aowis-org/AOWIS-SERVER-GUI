(function () {
    "use strict";

    const TILE_SIZE = 256;
    const REFERENCE_ZOOM = 18;
    const NETWORK_COLOR = "#000000";
    const SYMBOLOGY_VALUE_UNAVAILABLE_COLOR = "#000000";
    const RAMP_COLORS = [
        "#440154",
        "#443983",
        "#31688e",
        "#21918c",
        "#35b779",
        "#90d743",
        "#fde725"
    ];
    const NETWORK_IMAGE_PADDING = 8;
    const NETWORK_IMAGE_OVERSCAN_FACTOR = 3;
    const NETWORK_IMAGE_MAX_DIMENSION = 4096;
    const NETWORK_IMAGE_MAX_AREA = 8 * 1024 * 1024;
    const NETWORK_IMAGE_REBUILD_EDGE = 256;
    const HEATMAP_MAX_DIMENSION = 2048;
    const HEATMAP_MAX_AREA = 2 * 1024 * 1024;
    const HEATMAP_MAX_KERNEL_CACHE_PIXELS = 8 * 1024 * 1024;
    const HEATMAP_MAX_KERNEL_RADIUS = 256;
    const HEATMAP_MAX_COLOR_BUCKETS = 64;
    const MAX_SPATIAL_QUERY_CELLS = 4096;
    const LINK_HIT_DISTANCE = 7;
    const SPATIAL_CELL_SIZE = 128;
    const ENTITY_JUNCTION = 1;
    const ENTITY_RESERVOIR = 2;
    const ENTITY_TANK = 3;
    const ENTITY_PIPE = 4;
    const ENTITY_PUMP = 5;
    const ENTITY_VALVE = 6;
    const ICON_DEFINITIONS = new Map([
        [ENTITY_RESERVOIR, {
            file: "svg/reservoir.svg",
            symbolId: "aowis-network-reservoir",
            viewWidth: 186,
            viewHeight: 138,
            hitShape: "rectangle"
        }],
        [ENTITY_TANK, {
            file: "svg/tank.svg",
            symbolId: "aowis-network-tank",
            viewWidth: 138,
            viewHeight: 183,
            hitShape: "rectangle"
        }],
        [ENTITY_PUMP, {
            file: "svg/pump.svg",
            symbolId: "aowis-network-pump",
            viewWidth: 126,
            viewHeight: 110,
            hitShape: "rectangle"
        }],
        [ENTITY_VALVE, {
            file: "svg/valve.svg",
            symbolId: "aowis-network-valve",
            viewWidth: 138,
            viewHeight: 138,
            hitShape: "ellipse"
        }]
    ]);

    const state = {
        layer: null,
        image: null,
        imageObjectUrl: null,
        imageZoom: null,
        imageOffsetX: 0,
        imageOffsetY: 0,
        imageWidth: 0,
        imageHeight: 0,
        imageBounds: null,
        pendingImage: null,
        pendingImageObjectUrl: null,
        pendingImageZoom: null,
        pendingImageWidth: 0,
        pendingImageHeight: 0,
        pendingImageBounds: null,
        imageGeneration: 0,
        imageFrameRequest: 0,
        unsubscribeView: null,
        geometryOriginX: 0,
        geometryOriginY: 0,
        geometryMinimumX: 0,
        geometryMinimumY: 0,
        geometryMaximumX: 0,
        geometryMaximumY: 0,
        iconSymbols: "",
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
        nodeMinimum: 0,
        nodeMaximum: 0,
        nodeValues: new Map(),
        linkVisual: 0,
        linkThicknessPixels: 3,
        linkMinimum: 0,
        linkMaximum: 0,
        linkValues: new Map(),
        heatmapCanvas: null,
        heatmapZoom: null,
        heatmapOffsetX: 0,
        heatmapOffsetY: 0,
        heatmapWidth: 0,
        heatmapHeight: 0,
        heatmapBounds: null,
        heatmapFrameRequest: 0,
        heatmapVisual: 0,
        heatmapMinimum: 0,
        heatmapMaximum: 0,
        heatmapValues: new Map(),
        heatmapOpacity: 55,
        heatmapRadiusMeters: 100
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

    function nodeSizeScale() {
        return Math.max(0.5, Math.min(2.5, state.nodeSizePercent / 100));
    }

    function baseMarkerSizeForZoom(zoom) {
        return Math.max(10, Math.min(40, 10 + (zoom - 16) * 10));
    }

    function markerSizeForZoom(zoom) {
        return Math.max(5, baseMarkerSizeForZoom(zoom) * nodeSizeScale());
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
        const markerSize = markerSizeForZoom(zoom);
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

    function valueColor(visual, values, renderId, minimum, maximum) {
        if (visual === 0)
            return NETWORK_COLOR;
        if (!values.has(renderId))
            return SYMBOLOGY_VALUE_UNAVAILABLE_COLOR;

        const value = values.get(renderId);
        if (!Number.isFinite(value) || !Number.isFinite(minimum) || !Number.isFinite(maximum))
            return SYMBOLOGY_VALUE_UNAVAILABLE_COLOR;
        if (minimum === maximum)
            return rampColor(0.5);

        return rampColor((value - minimum) / (maximum - minimum));
    }

    function nodeColor(renderId) {
        return valueColor(
            state.nodeVisual, state.nodeValues, renderId, state.nodeMinimum, state.nodeMaximum);
    }

    function linkColor(renderId) {
        return valueColor(
            state.linkVisual, state.linkValues, renderId, state.linkMinimum, state.linkMaximum);
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

    function revokeObjectUrl(objectUrl) {
        if (objectUrl)
            URL.revokeObjectURL(objectUrl);
    }

    function clearScheduledImage() {
        if (state.imageFrameRequest !== 0)
            window.cancelAnimationFrame(state.imageFrameRequest);
        state.imageFrameRequest = 0;
    }

    function clearPendingImage() {
        ++state.imageGeneration;
        if (state.pendingImage)
            state.pendingImage.remove();
        revokeObjectUrl(state.pendingImageObjectUrl);
        state.pendingImage = null;
        state.pendingImageObjectUrl = null;
        state.pendingImageZoom = null;
        state.pendingImageWidth = 0;
        state.pendingImageHeight = 0;
        state.pendingImageBounds = null;
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
        state.imageWidth = 0;
        state.imageHeight = 0;
        state.imageBounds = null;
    }

    function clearNetworkImage() {
        clearScheduledImage();
        clearPendingImage();
        clearRenderedImage();
    }

    function clearScheduledHeatmap() {
        if (state.heatmapFrameRequest !== 0)
            window.cancelAnimationFrame(state.heatmapFrameRequest);
        state.heatmapFrameRequest = 0;
    }

    function clearRenderedHeatmap() {
        if (state.heatmapCanvas)
            state.heatmapCanvas.remove();
        state.heatmapCanvas = null;
        state.heatmapZoom = null;
        state.heatmapOffsetX = 0;
        state.heatmapOffsetY = 0;
        state.heatmapWidth = 0;
        state.heatmapHeight = 0;
        state.heatmapBounds = null;
    }

    function clearHeatmap() {
        clearScheduledHeatmap();
        clearRenderedHeatmap();
    }

    function iconGeometryElements(svgElement) {
        const supportedElements = new Set([
            "circle", "ellipse", "g", "line", "path", "polygon", "polyline", "rect", "text"
        ]);
        const serializer = new XMLSerializer();
        const result = [];
        for (const child of svgElement.children) {
            if (supportedElements.has(child.localName))
                result.push(serializer.serializeToString(child));
        }
        return result.join("");
    }

    async function loadIconSymbol(definition) {
        try {
            const response = await fetch(definition.file, { cache: "force-cache" });
            if (!response.ok)
                throw new Error(`HTTP ${response.status}`);

            const documentSvg = new DOMParser().parseFromString(
                await response.text(), "image/svg+xml");
            if (documentSvg.querySelector("parsererror"))
                throw new Error("invalid SVG");

            const svgElement = documentSvg.documentElement;
            const geometry = iconGeometryElements(svgElement).replace(/#000000/gi, "currentColor");
            if (!geometry)
                throw new Error("SVG contains no supported geometry");

            return `<symbol id="${definition.symbolId}" viewBox="0 0 ${definition.viewWidth} ${definition.viewHeight}" fill="none">${geometry}</symbol>`;
        } catch (error) {
            console.error(`Failed to load AOWIS network icon ${definition.file}:`, error);
            return "";
        }
    }

    async function loadIconSymbols() {
        const symbols = await Promise.all(Array.from(ICON_DEFINITIONS.values()).map(
            definition => loadIconSymbol(definition)));
        state.iconSymbols = symbols.join("");
        clearNetworkImage();
        if (state.lastMapView)
            handleMapViewChanged(state.lastMapView);
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

    function boundedHeatmapRasterDimensions(displayWidth, displayHeight) {
        const displayArea = Math.max(1, displayWidth * displayHeight);
        const factor = Math.min(
            1,
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

    function intersectBounds(first, second) {
        const result = {
            minimumX: Math.max(first.minimumX, second.minimumX),
            minimumY: Math.max(first.minimumY, second.minimumY),
            maximumX: Math.min(first.maximumX, second.maximumX),
            maximumY: Math.min(first.maximumY, second.maximumY)
        };
        if (result.minimumX >= result.maximumX || result.minimumY >= result.maximumY)
            return null;
        return result;
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

    function visibleLinkPathDataByColor(bounds) {
        const pathDataByColor = new Map();
        for (const collectionName of ["pipeSegments", "deviceSegments"]) {
            const segments = collectionForSpatialQuery(collectionName);
            for (const index of indicesInWorldBounds(bounds, collectionName)) {
                const segment = segments[index];
                if (!segmentIntersectsBounds(segment, bounds))
                    continue;

                const color = linkColor(segment.renderId);
                const pathCommands = pathDataByColor.get(color) || [];
                pathCommands.push(
                    "M", formatted(segment.x1 - state.geometryOriginX), " ",
                    formatted(segment.y1 - state.geometryOriginY),
                    "L", formatted(segment.x2 - state.geometryOriginX), " ",
                    formatted(segment.y2 - state.geometryOriginY));
                pathDataByColor.set(color, pathCommands);
            }
        }

        const result = new Map();
        for (const [color, pathCommands] of pathDataByColor)
            result.set(color, pathCommands.join(""));
        return result;
    }

    function networkImageSpecification(mapView) {
        const zoom = mapView.zoom;
        const scale = scaleForZoom(zoom);
        const cacheDimensions = boundedImageDimensions(mapView);
        const transform = worldTransform(mapView);
        const centerX = state.geometryOriginX
            + (mapView.width / 2 - transform.translateX) / scale;
        const centerY = state.geometryOriginY
            + (mapView.height / 2 - transform.translateY) / scale;
        const cacheBounds = {
            minimumX: centerX - cacheDimensions.width / (2 * scale),
            minimumY: centerY - cacheDimensions.height / (2 * scale),
            maximumX: centerX + cacheDimensions.width / (2 * scale),
            maximumY: centerY + cacheDimensions.height / (2 * scale)
        };
        const geometryPadding = (markerSizeForZoom(zoom) / 2
            + NETWORK_IMAGE_PADDING) / scale;
        const geometryBounds = expandedBounds({
            minimumX: state.geometryMinimumX,
            minimumY: state.geometryMinimumY,
            maximumX: state.geometryMaximumX,
            maximumY: state.geometryMaximumY
        }, geometryPadding);
        const visibleBounds = intersectBounds(cacheBounds, geometryBounds);
        const renderBounds = visibleBounds || {
            minimumX: cacheBounds.minimumX,
            minimumY: cacheBounds.minimumY,
            maximumX: cacheBounds.minimumX + 1 / scale,
            maximumY: cacheBounds.minimumY + 1 / scale
        };
        const width = visibleBounds ? Math.min(cacheDimensions.width, Math.max(1,
            Math.ceil((renderBounds.maximumX - renderBounds.minimumX) * scale))) : 1;
        const height = visibleBounds ? Math.min(cacheDimensions.height, Math.max(1,
            Math.ceil((renderBounds.maximumY - renderBounds.minimumY) * scale))) : 1;
        const offsetX = (renderBounds.minimumX - state.geometryOriginX) * scale;
        const offsetY = (renderBounds.minimumY - state.geometryOriginY) * scale;
        const translateX = -offsetX;
        const translateY = -offsetY;
        const markerElements = [];
        if (visibleBounds) {
            const markerQueryPadding = markerSizeForZoom(zoom) / (2 * scale)
                + NETWORK_IMAGE_PADDING / scale;
            const markerQueryBounds = expandedBounds(renderBounds, markerQueryPadding);
            for (const index of indicesInWorldBounds(markerQueryBounds, "markers")) {
                const marker = state.markers[index];
                if (!markerIntersectsBounds(marker, renderBounds, zoom, scale))
                    continue;

                const centerMarkerX = translateX
                    + (marker.x - state.geometryOriginX) * scale;
                const centerMarkerY = translateY
                    + (marker.y - state.geometryOriginY) * scale;
                const color = markerColor(marker);
                if (marker.entityType === ENTITY_JUNCTION) {
                    markerElements.push(
                        `<circle cx="${formatted(centerMarkerX)}" cy="${formatted(centerMarkerY)}" r="${formatted(junctionDotDiameterForZoom(zoom) / 2)}" fill="${color}"/>`);
                    continue;
                }

                const definition = ICON_DEFINITIONS.get(marker.entityType);
                if (!definition)
                    continue;

                const markerBounds = markerScreenBounds(marker.entityType, zoom);
                markerElements.push(
                    `<use href="#${definition.symbolId}" x="${formatted(centerMarkerX - markerBounds.width / 2)}" y="${formatted(centerMarkerY - markerBounds.height / 2)}" width="${formatted(markerBounds.width)}" height="${formatted(markerBounds.height)}" color="${color}"/>`);
            }
        }

        const linkBounds = visibleBounds
            ? expandedBounds(renderBounds,
                (state.linkThicknessPixels / 2 + NETWORK_IMAGE_PADDING) / scale)
            : null;
        const linkPathDataByColor = linkBounds ? visibleLinkPathDataByColor(linkBounds) : new Map();
        const linkElements = [];
        for (const [color, pathData] of linkPathDataByColor) {
            linkElements.push(
                `<path fill="none" stroke="${color}" stroke-width="${formatted(state.linkThicknessPixels)}" stroke-linecap="round" stroke-linejoin="round" vector-effect="non-scaling-stroke" d="${pathData}"/>`);
        }
        const svg = [
            `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}">`,
            `<defs>${state.iconSymbols}</defs>`,
            `<g transform="translate(${formatted(translateX)} ${formatted(translateY)}) scale(${formattedScale(scale)})">`,
            linkElements.join(""),
            "</g>",
            markerElements.join(""),
            "</svg>"
        ].join("");
        return {
            svg: svg,
            width: width,
            height: height,
            cacheWidth: cacheDimensions.width,
            cacheHeight: cacheDimensions.height,
            bounds: cacheBounds,
            offsetX: offsetX,
            offsetY: offsetY
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

    function renderedImageCoversMapView(mapView) {
        return Boolean(state.image && state.imageZoom === mapView.zoom
            && imageBoundsCoverMapView(
                state.imageBounds, state.imageWidth, state.imageHeight, mapView));
    }

    function pendingImageCoversMapView(mapView) {
        if (!state.pendingImage || state.pendingImageZoom !== mapView.zoom
            || !state.pendingImageBounds) {
            return false;
        }

        const horizontalOverscan = Math.max(
            0, (state.pendingImageWidth - mapView.width) / 2);
        const verticalOverscan = Math.max(
            0, (state.pendingImageHeight - mapView.height) / 2);
        const horizontalSafety = Math.min(
            NETWORK_IMAGE_REBUILD_EDGE, horizontalOverscan / 2);
        const verticalSafety = Math.min(
            NETWORK_IMAGE_REBUILD_EDGE, verticalOverscan / 2);
        return boundsContain(state.pendingImageBounds, mapViewWorldBounds(
            mapView, horizontalSafety, verticalSafety));
    }

    function requestNetworkImage(mapView) {
        if (!state.layer || !state.geometryReady || !shouldDisplayNetwork(mapView))
            return;
        if (renderedImageCoversMapView(mapView) || pendingImageCoversMapView(mapView))
            return;

        clearPendingImage();
        const generation = state.imageGeneration;
        const specification = networkImageSpecification(mapView);
        const zoom = mapView.zoom;
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
        image.style.zIndex = "2";

        state.pendingImage = image;
        state.pendingImageObjectUrl = objectUrl;
        state.pendingImageZoom = zoom;
        state.pendingImageWidth = specification.cacheWidth;
        state.pendingImageHeight = specification.cacheHeight;
        state.pendingImageBounds = specification.bounds;

        image.addEventListener("load", () => {
            if (generation !== state.imageGeneration || state.pendingImage !== image) {
                revokeObjectUrl(objectUrl);
                return;
            }
            if (!state.lastMapView || state.lastMapView.zoom !== zoom
                || !shouldDisplayNetwork(state.lastMapView)) {
                state.pendingImage = null;
                state.pendingImageObjectUrl = null;
                state.pendingImageZoom = null;
                state.pendingImageWidth = 0;
                state.pendingImageHeight = 0;
                state.pendingImageBounds = null;
                revokeObjectUrl(objectUrl);
                return;
            }

            clearRenderedImage();
            state.pendingImage = null;
            state.pendingImageObjectUrl = null;
            state.pendingImageZoom = null;
            state.pendingImageWidth = 0;
            state.pendingImageHeight = 0;
            state.pendingImageBounds = null;
            state.image = image;
            state.imageObjectUrl = objectUrl;
            state.imageZoom = zoom;
            state.imageOffsetX = specification.offsetX;
            state.imageOffsetY = specification.offsetY;
            state.imageWidth = specification.cacheWidth;
            state.imageHeight = specification.cacheHeight;
            state.imageBounds = specification.bounds;
            state.layer.appendChild(image);
            positionNetworkImage(state.lastMapView);
            if (!renderedImageCoversMapView(state.lastMapView))
                scheduleNetworkImage();
        }, { once: true });

        image.addEventListener("error", () => {
            if (state.pendingImage === image) {
                state.pendingImage = null;
                state.pendingImageObjectUrl = null;
                state.pendingImageZoom = null;
                state.pendingImageWidth = 0;
                state.pendingImageHeight = 0;
                state.pendingImageBounds = null;
            }
            revokeObjectUrl(objectUrl);
            console.error("Failed to rasterize AOWIS browser network SVG");
        }, { once: true });

        image.src = objectUrl;
    }

    function scheduleNetworkImage() {
        if (state.imageFrameRequest !== 0)
            return;

        state.imageFrameRequest = window.requestAnimationFrame(() => {
            state.imageFrameRequest = 0;
            const mapView = state.lastMapView;
            if (!mapView || !shouldDisplayNetwork(mapView))
                return;
            requestNetworkImage(mapView);
        });
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

    function applyHeatmapOpacity() {
        if (!state.heatmapCanvas)
            return;

        state.heatmapCanvas.style.opacity = String(
            Math.max(0, Math.min(100, state.heatmapOpacity)) / 100);
    }

    function heatmapSpecification(mapView) {
        const zoom = mapView.zoom;
        const scale = scaleForZoom(zoom);
        const displayDimensions = boundedImageDimensions(mapView);
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
        const gradient = context.createRadialGradient(
            center, center, 0, center, center, kernelRadius);
        gradient.addColorStop(0, `rgba(${red}, ${green}, ${blue}, 1)`);
        gradient.addColorStop(0.2, `rgba(${red}, ${green}, ${blue}, 1)`);
        gradient.addColorStop(0.55, `rgba(${red}, ${green}, ${blue}, 0.5)`);
        gradient.addColorStop(1, `rgba(${red}, ${green}, ${blue}, 0)`);
        context.fillStyle = gradient;
        context.fillRect(0, 0, diameter, diameter);
        return {
            canvas: canvas,
            diameter: Math.max(3, Math.ceil(boundedRadius * 2) + 2)
        };
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
        const kernelCache = new Map();
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
            let kernel = kernelCache.get(bucket);
            if (!kernel) {
                const bucketFraction = bucket / (colorBucketCount - 1);
                kernel = createHeatmapKernel(radius, rampRgb(bucketFraction));
                if (!kernel)
                    return null;
                kernelCache.set(bucket, kernel);
            }
            context.drawImage(
                kernel.canvas,
                x - kernel.diameter / 2,
                y - kernel.diameter / 2,
                kernel.diameter,
                kernel.diameter);
        }

        canvas.setAttribute("aria-hidden", "true");
        canvas.style.position = "absolute";
        canvas.style.left = "0";
        canvas.style.top = "0";
        canvas.style.width = `${specification.displayWidth}px`;
        canvas.style.height = `${specification.displayHeight}px`;
        canvas.style.display = "none";
        canvas.style.pointerEvents = "none";
        canvas.style.transformOrigin = "0 0";
        canvas.style.zIndex = "1";
        canvas.style.imageRendering = "auto";
        return canvas;
    }

    function positionHeatmap(mapView) {
        if (!state.heatmapCanvas || state.heatmapZoom !== mapView.zoom)
            return;

        const transform = worldTransform(mapView);
        const x = snapToPhysicalPixel(transform.translateX + state.heatmapOffsetX);
        const y = snapToPhysicalPixel(transform.translateY + state.heatmapOffsetY);
        state.heatmapCanvas.style.transform = `translate3d(${x}px, ${y}px, 0)`;
        state.heatmapCanvas.style.display = shouldDisplayHeatmap(mapView)
            ? "block" : "none";
        applyHeatmapOpacity();
    }

    function renderedHeatmapCoversMapView(mapView) {
        return Boolean(state.heatmapCanvas && state.heatmapZoom === mapView.zoom
            && imageBoundsCoverMapView(
                state.heatmapBounds, state.heatmapWidth, state.heatmapHeight, mapView));
    }

    function requestHeatmap(mapView) {
        if (!state.layer || !shouldDisplayHeatmap(mapView)
            || renderedHeatmapCoversMapView(mapView)) {
            return;
        }

        const specification = heatmapSpecification(mapView);
        const canvas = renderHeatmap(specification);
        if (!canvas)
            return;

        clearRenderedHeatmap();
        state.heatmapCanvas = canvas;
        state.heatmapZoom = specification.zoom;
        state.heatmapOffsetX = specification.offsetX;
        state.heatmapOffsetY = specification.offsetY;
        state.heatmapWidth = specification.displayWidth;
        state.heatmapHeight = specification.displayHeight;
        state.heatmapBounds = specification.bounds;
        state.layer.appendChild(canvas);
        positionHeatmap(mapView);
    }

    function scheduleHeatmap() {
        if (state.heatmapFrameRequest !== 0)
            return;

        state.heatmapFrameRequest = window.requestAnimationFrame(() => {
            state.heatmapFrameRequest = 0;
            const mapView = state.lastMapView;
            if (!mapView || !shouldDisplayHeatmap(mapView))
                return;
            requestHeatmap(mapView);
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
        clearNetworkImage();
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
                segmentCollection.push({
                    renderId: projectedLink.renderId,
                    entityType: projectedLink.entityType,
                    x1: vertices[index - 1].x,
                    y1: vertices[index - 1].y,
                    x2: vertices[index].x,
                    y2: vertices[index].y
                });
            }

            if (projectedLink.entityType === ENTITY_PUMP || projectedLink.entityType === ENTITY_VALVE) {
                const center = polylineMidpoint(vertices);
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

        if (state.pendingImage && state.pendingImageZoom !== mapView.zoom)
            clearPendingImage();

        if (state.heatmapCanvas && state.heatmapZoom === mapView.zoom)
            positionHeatmap(mapView);
        else if (state.heatmapCanvas)
            state.heatmapCanvas.style.display = "none";

        if (state.image && state.imageZoom === mapView.zoom)
            positionNetworkImage(mapView);
        else if (state.image)
            state.image.style.display = "none";

        if (shouldDisplayHeatmap(mapView)
            && !renderedHeatmapCoversMapView(mapView)) {
            scheduleHeatmap();
        }

        if (!renderedImageCoversMapView(mapView)
            && !pendingImageCoversMapView(mapView)) {
            scheduleNetworkImage();
        }
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
        loadIconSymbols();
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
        const worldTolerance = markerSizeForZoom(zoom) / (2 * scale);
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
            10, Math.min(500, Number(symbology.heatmapRadiusMeters) || 100));

        const networkChanged = state.nodeVisual !== nodeVisual
            || state.nodeSizePercent !== nodeSizePercent
            || state.nodeMinimum !== nodeMinimum
            || state.nodeMaximum !== nodeMaximum
            || !symbologyValuesEqual(state.nodeValues, nodeValues)
            || state.linkVisual !== linkVisual
            || state.linkThicknessPixels !== linkThicknessPixels
            || state.linkMinimum !== linkMinimum
            || state.linkMaximum !== linkMaximum
            || !symbologyValuesEqual(state.linkValues, linkValues);
        const heatmapChanged = state.heatmapVisual !== heatmapVisual
            || state.heatmapMinimum !== heatmapMinimum
            || state.heatmapMaximum !== heatmapMaximum
            || state.heatmapRadiusMeters !== heatmapRadiusMeters
            || !symbologyValuesEqual(state.heatmapValues, heatmapValues);
        const heatmapOpacityChanged = state.heatmapOpacity !== heatmapOpacity;

        state.nodeVisual = nodeVisual;
        state.nodeSizePercent = nodeSizePercent;
        state.nodeMinimum = nodeMinimum;
        state.nodeMaximum = nodeMaximum;
        state.nodeValues = nodeValues;
        state.linkVisual = linkVisual;
        state.linkThicknessPixels = linkThicknessPixels;
        state.linkMinimum = linkMinimum;
        state.linkMaximum = linkMaximum;
        state.linkValues = linkValues;
        state.heatmapVisual = heatmapVisual;
        state.heatmapMinimum = heatmapMinimum;
        state.heatmapMaximum = heatmapMaximum;
        state.heatmapValues = heatmapValues;
        state.heatmapOpacity = heatmapOpacity;
        state.heatmapRadiusMeters = heatmapRadiusMeters;

        if (networkChanged)
            clearNetworkImage();
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

        clearNetworkImage();
        clearHeatmap();
        if (state.layer)
            state.layer.remove();
        state.layer = null;
        state.geometryOriginX = 0;
        state.geometryOriginY = 0;
        state.geometryMinimumX = 0;
        state.geometryMinimumY = 0;
        state.geometryMaximumX = 0;
        state.geometryMaximumY = 0;
        state.iconSymbols = "";
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
        state.nodeMinimum = 0;
        state.nodeMaximum = 0;
        state.nodeValues = new Map();
        state.linkVisual = 0;
        state.linkThicknessPixels = 3;
        state.linkMinimum = 0;
        state.linkMaximum = 0;
        state.linkValues = new Map();
        state.heatmapVisual = 0;
        state.heatmapMinimum = 0;
        state.heatmapMaximum = 0;
        state.heatmapValues = new Map();
        state.heatmapOpacity = 55;
        state.heatmapRadiusMeters = 100;
    }

    window.aowisBrowserNetwork = {
        setSnapshot: setSnapshot,
        setSymbology: setSymbology,
        setBackground: setBackground,
        setOwnerId: setOwnerId,
        hitTest: hitTest,
        destroy: destroy
    };

    initialize();
})();
