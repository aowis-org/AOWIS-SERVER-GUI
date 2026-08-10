(function () {
    "use strict";

    const ENTITY_JUNCTION = 1;
    const ENTITY_RESERVOIR = 2;
    const ENTITY_TANK = 3;
    const ENTITY_PUMP = 5;
    const ENTITY_VALVE = 6;
    const NETWORK_COLOR = "#000000";
    const RAMP_COLORS = [
        "#440154",
        "#443983",
        "#31688e",
        "#21918c",
        "#35b779",
        "#90d743",
        "#fde725"
    ];
    const PALETTE_SIZE = 64;
    const HEATMAP_MAX_PIXEL_RATIO = 0.5;
    const HEATMAP_MIN_PIXEL_RATIO = 0.0625;
    const HEATMAP_TARGET_RADIUS_PIXELS = 96;
    const HEATMAP_TARGET_FRAGMENT_BUDGET = 24 * 1024 * 1024;

    let role = "";

    const network = {
        markerCoordinates: new Float64Array(0),
        markerMetadata: new Uint32Array(0),
        pipeCoordinates: new Float64Array(0),
        pipeRenderIds: new Uint32Array(0),
        deviceCoordinates: new Float64Array(0),
        deviceRenderIds: new Uint32Array(0),
        geometryOriginX: 0,
        geometryOriginY: 0,
        nodeVisual: 0,
        nodeSizePercent: 100,
        iconSizePercent: 100,
        nodeMinimum: 0,
        nodeMaximum: 0,
        nodeValues: new Map(),
        linkVisual: 0,
        linkThicknessPixels: 3,
        linkMinimum: 0,
        linkMaximum: 0,
        linkValues: new Map(),
        icons: new Map(),
        iconsReady: Promise.resolve(),
        tintedIcons: new Map(),
        canvas: null
    };

    const heatmap = {
        canvas: null,
        gl: null,
        program: null,
        buffer: null,
        locations: null,
        nodeCoordinates: new Float64Array(0),
        nodeRenderIds: new Uint32Array(0),
        nodeIndexByRenderId: new Map(),
        vertexCount: 0,
        minimum: 0,
        maximum: 0,
        dataReady: false
    };

    const palette = buildPalette(PALETTE_SIZE);

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

    function buildPalette(size) {
        const result = [];
        for (let index = 0; index < size; ++index) {
            const color = rampRgb(size <= 1 ? 0.5 : index / (size - 1));
            result.push(`#${hexadecimalByte(color.red)}${hexadecimalByte(color.green)}${hexadecimalByte(color.blue)}`);
        }
        return result;
    }

    function paletteIndex(value, minimum, maximum) {
        if (!Number.isFinite(value) || !Number.isFinite(minimum) || !Number.isFinite(maximum))
            return -1;
        if (minimum === maximum)
            return Math.floor((PALETTE_SIZE - 1) / 2);
        const fraction = Math.max(0, Math.min(1, (value - minimum) / (maximum - minimum)));
        return Math.max(0, Math.min(PALETTE_SIZE - 1, Math.round(fraction * (PALETTE_SIZE - 1))));
    }

    function valueColor(visual, values, renderId, minimum, maximum) {
        if (visual === 0 || !values.has(renderId))
            return NETWORK_COLOR;
        const index = paletteIndex(values.get(renderId), minimum, maximum);
        return index < 0 ? NETWORK_COLOR : palette[index];
    }

    function nodeColor(renderId) {
        return valueColor(network.nodeVisual, network.nodeValues, renderId,
            network.nodeMinimum, network.nodeMaximum);
    }

    function linkColor(renderId) {
        return valueColor(network.linkVisual, network.linkValues, renderId,
            network.linkMinimum, network.linkMaximum);
    }

    function markerColor(entityType, renderId) {
        if (entityType === ENTITY_JUNCTION || entityType === ENTITY_RESERVOIR || entityType === ENTITY_TANK)
            return nodeColor(renderId);
        return linkColor(renderId);
    }

    function baseMarkerSizeForZoom(zoom) {
        return Math.max(10, Math.min(40, 10 + (zoom - 16) * 10));
    }

    function nodeSizeScale() {
        return Math.max(0.5, Math.min(2.5, network.nodeSizePercent / 100));
    }

    function iconSizeScale() {
        return Math.max(0.5, Math.min(2.5, network.iconSizePercent / 100));
    }

    function junctionDiameterForZoom(zoom) {
        const baseDiameter = Math.max(8, Math.min(12, baseMarkerSizeForZoom(zoom) * 0.3));
        return Math.max(4, baseDiameter * nodeSizeScale());
    }

    function markerDimensions(entityType, zoom) {
        if (entityType === ENTITY_JUNCTION) {
            const size = Math.max(5, baseMarkerSizeForZoom(zoom) * nodeSizeScale());
            return { width: size, height: size };
        }
        const size = Math.max(5, baseMarkerSizeForZoom(zoom) * iconSizeScale());
        if (entityType === ENTITY_RESERVOIR)
            return { width: size, height: size * 138 / 186 };
        if (entityType === ENTITY_TANK)
            return { width: size * 138 / 183, height: size };
        if (entityType === ENTITY_PUMP)
            return { width: size, height: size * 110 / 126 };
        return { width: size, height: size };
    }

    function valuesFromTypedArrays(ids, values) {
        const result = new Map();
        const count = Math.min(ids.length, values.length);
        for (let index = 0; index < count; ++index)
            result.set(ids[index], values[index]);
        return result;
    }

    function markerIntersectsBounds(x, y, entityType, bounds, zoom, scale) {
        if (!bounds)
            return true;
        const dimensions = markerDimensions(entityType, zoom);
        const halfWidth = dimensions.width / (2 * scale);
        const halfHeight = dimensions.height / (2 * scale);
        return x + halfWidth >= bounds.minimumX && x - halfWidth <= bounds.maximumX
            && y + halfHeight >= bounds.minimumY && y - halfHeight <= bounds.maximumY;
    }

    function segmentIntersectsBounds(x1, y1, x2, y2, bounds) {
        if (!bounds)
            return true;
        return Math.max(x1, x2) >= bounds.minimumX && Math.min(x1, x2) <= bounds.maximumX
            && Math.max(y1, y2) >= bounds.minimumY && Math.min(y1, y2) <= bounds.maximumY;
    }

    function networkPoint(x, y, request) {
        if (request.fallback) {
            return {
                x: request.translateX + (x - network.geometryOriginX) * request.scale,
                y: request.translateY + (y - network.geometryOriginY) * request.scale
            };
        }
        return {
            x: (x - request.originX) * request.scale,
            y: (y - request.originY) * request.scale
        };
    }

    async function loadNetworkIcons(baseUrl) {
        const definitions = [
            [ENTITY_RESERVOIR, "reservoir.svg"],
            [ENTITY_TANK, "tank.svg"],
            [ENTITY_PUMP, "pump.svg"],
            [ENTITY_VALVE, "valve.svg"]
        ];
        await Promise.all(definitions.map(async ([entityType, filename]) => {
            try {
                const response = await fetch(new URL(filename, baseUrl).href, { cache: "force-cache" });
                if (!response.ok)
                    throw new Error(`HTTP ${response.status}`);
                const blob = await response.blob();
                const bitmap = await createImageBitmap(blob);
                network.icons.set(entityType, bitmap);
            } catch (error) {
                console.error(`AOWIS monitor worker failed to load ${filename}:`, error);
            }
        }));
    }

    function tintedIcon(entityType, width, height, color, ratio) {
        const source = network.icons.get(entityType);
        if (!source || !(width > 0) || !(height > 0))
            return null;
        const physicalWidth = Math.max(1, Math.ceil(width * ratio));
        const physicalHeight = Math.max(1, Math.ceil(height * ratio));
        const key = `${entityType}:${physicalWidth}:${physicalHeight}:${color}`;
        const cached = network.tintedIcons.get(key);
        if (cached)
            return cached;

        const canvas = new OffscreenCanvas(physicalWidth, physicalHeight);
        const context = canvas.getContext("2d", { alpha: true });
        if (!context)
            return null;
        context.setTransform(ratio, 0, 0, ratio, 0, 0);
        context.clearRect(0, 0, width, height);
        context.drawImage(source, 0, 0, width, height);
        context.globalCompositeOperation = "source-in";
        context.fillStyle = color;
        context.fillRect(0, 0, width, height);
        context.globalCompositeOperation = "source-over";
        network.tintedIcons.set(key, canvas);
        return canvas;
    }

    function drawSegmentCollection(context, coordinates, renderIds, request) {
        const groups = new Map();
        const count = Math.min(renderIds.length, Math.floor(coordinates.length / 4));
        for (let index = 0; index < count; ++index) {
            const base = index * 4;
            const x1 = coordinates[base];
            const y1 = coordinates[base + 1];
            const x2 = coordinates[base + 2];
            const y2 = coordinates[base + 3];
            if (request.fallback && !segmentIntersectsBounds(x1, y1, x2, y2, request.viewBounds))
                continue;
            const color = linkColor(renderIds[index]);
            let segments = groups.get(color);
            if (!segments) {
                segments = [];
                groups.set(color, segments);
            }
            segments.push(index);
        }

        context.lineWidth = network.linkThicknessPixels;
        context.lineCap = "round";
        context.lineJoin = "round";
        for (const [color, indices] of groups) {
            context.beginPath();
            for (const index of indices) {
                const base = index * 4;
                const first = networkPoint(coordinates[base], coordinates[base + 1], request);
                const second = networkPoint(coordinates[base + 2], coordinates[base + 3], request);
                context.moveTo(first.x, first.y);
                context.lineTo(second.x, second.y);
            }
            context.strokeStyle = color;
            context.stroke();
        }
    }

    function drawMarkers(context, request) {
        const junctionGroups = new Map();
        const icons = [];
        const markerCount = Math.min(
            Math.floor(network.markerCoordinates.length / 2),
            Math.floor(network.markerMetadata.length / 2));
        for (let index = 0; index < markerCount; ++index) {
            const coordinateBase = index * 2;
            const metadataBase = index * 2;
            const x = network.markerCoordinates[coordinateBase];
            const y = network.markerCoordinates[coordinateBase + 1];
            const renderId = network.markerMetadata[metadataBase];
            const entityType = network.markerMetadata[metadataBase + 1];
            if (request.fallback && !markerIntersectsBounds(
                x, y, entityType, request.viewBounds, request.zoom, request.scale)) {
                continue;
            }
            const color = markerColor(entityType, renderId);
            const point = networkPoint(x, y, request);
            if (entityType === ENTITY_JUNCTION) {
                let points = junctionGroups.get(color);
                if (!points) {
                    points = [];
                    junctionGroups.set(color, points);
                }
                points.push(point.x, point.y);
            } else {
                icons.push({ entityType: entityType, color: color, x: point.x, y: point.y });
            }
        }

        const radius = junctionDiameterForZoom(request.zoom) / 2;
        for (const [color, points] of junctionGroups) {
            context.beginPath();
            for (let index = 0; index + 1 < points.length; index += 2) {
                context.moveTo(points[index] + radius, points[index + 1]);
                context.arc(points[index], points[index + 1], radius, 0, Math.PI * 2);
            }
            context.fillStyle = color;
            context.fill();
        }

        for (const icon of icons) {
            const dimensions = markerDimensions(icon.entityType, request.zoom);
            const image = tintedIcon(
                icon.entityType, dimensions.width, dimensions.height, icon.color, request.ratio);
            if (!image)
                continue;
            context.drawImage(
                image,
                icon.x - dimensions.width / 2,
                icon.y - dimensions.height / 2,
                dimensions.width,
                dimensions.height);
        }
    }

    async function renderNetwork(request) {
        await network.iconsReady;
        const physicalWidth = Math.max(1, Math.ceil(request.width * request.ratio));
        const physicalHeight = Math.max(1, Math.ceil(request.height * request.ratio));
        if (!network.canvas || network.canvas.width !== physicalWidth || network.canvas.height !== physicalHeight)
            network.canvas = new OffscreenCanvas(physicalWidth, physicalHeight);
        const context = network.canvas.getContext("2d", { alpha: true });
        if (!context)
            throw new Error("OffscreenCanvas 2D context unavailable for network worker");
        context.setTransform(request.ratio, 0, 0, request.ratio, 0, 0);
        context.clearRect(0, 0, request.width, request.height);
        context.imageSmoothingEnabled = true;

        drawSegmentCollection(context, network.pipeCoordinates, network.pipeRenderIds, request);
        drawSegmentCollection(context, network.deviceCoordinates, network.deviceRenderIds, request);
        drawMarkers(context, request);

        return network.canvas.transferToImageBitmap();
    }

    function compileShader(gl, type, source) {
        const shader = gl.createShader(type);
        if (!shader)
            return null;
        gl.shaderSource(shader, source);
        gl.compileShader(shader);
        if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
            const message = gl.getShaderInfoLog(shader);
            gl.deleteShader(shader);
            throw new Error(`Heatmap shader compilation failed: ${message}`);
        }
        return shader;
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
            throw new Error("WebGL unavailable in heatmap worker");

        const vertexShader = compileShader(gl, gl.VERTEX_SHADER, `
            precision highp float;
            attribute vec2 a_local;
            attribute vec2 a_corner;
            attribute float a_value;
            uniform vec2 u_translate;
            uniform float u_scale;
            uniform vec2 u_viewport;
            uniform float u_radius;
            uniform float u_minimum;
            uniform float u_maximum;
            varying vec2 v_corner;
            varying float v_fraction;
            void main() {
                vec2 screen = u_translate + a_local * u_scale + a_corner * u_radius;
                vec2 clip = vec2(screen.x / u_viewport.x * 2.0 - 1.0,
                    1.0 - screen.y / u_viewport.y * 2.0);
                gl_Position = vec4(clip, 0.0, 1.0);
                v_corner = a_corner;
                float range = u_maximum - u_minimum;
                v_fraction = abs(range) < 0.0000001 ? 0.5
                    : clamp((a_value - u_minimum) / range, 0.0, 1.0);
            }
        `);
        const fragmentShader = compileShader(gl, gl.FRAGMENT_SHADER, `
            precision mediump float;
            uniform float u_solid_center;
            varying vec2 v_corner;
            varying float v_fraction;

            vec3 ramp(float fraction) {
                float scaled = clamp(fraction, 0.0, 1.0) * 6.0;
                if (scaled < 1.0)
                    return mix(vec3(68.0, 1.0, 84.0), vec3(68.0, 57.0, 131.0), scaled) / 255.0;
                if (scaled < 2.0)
                    return mix(vec3(68.0, 57.0, 131.0), vec3(49.0, 104.0, 142.0), scaled - 1.0) / 255.0;
                if (scaled < 3.0)
                    return mix(vec3(49.0, 104.0, 142.0), vec3(33.0, 145.0, 140.0), scaled - 2.0) / 255.0;
                if (scaled < 4.0)
                    return mix(vec3(33.0, 145.0, 140.0), vec3(53.0, 183.0, 121.0), scaled - 3.0) / 255.0;
                if (scaled < 5.0)
                    return mix(vec3(53.0, 183.0, 121.0), vec3(144.0, 215.0, 67.0), scaled - 4.0) / 255.0;
                return mix(vec3(144.0, 215.0, 67.0), vec3(253.0, 231.0, 37.0), scaled - 5.0) / 255.0;
            }

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
                gl_FragColor = vec4(ramp(v_fraction), clamp(alpha, 0.0, 1.0));
            }
        `);
        const program = gl.createProgram();
        if (!program)
            throw new Error("Unable to create heatmap WebGL program");
        gl.attachShader(program, vertexShader);
        gl.attachShader(program, fragmentShader);
        gl.linkProgram(program);
        gl.deleteShader(vertexShader);
        gl.deleteShader(fragmentShader);
        if (!gl.getProgramParameter(program, gl.LINK_STATUS))
            throw new Error(`Heatmap program linking failed: ${gl.getProgramInfoLog(program)}`);

        const buffer = gl.createBuffer();
        if (!buffer)
            throw new Error("Unable to create heatmap WebGL buffer");

        heatmap.canvas = canvas;
        heatmap.gl = gl;
        heatmap.program = program;
        heatmap.buffer = buffer;
        heatmap.locations = {
            local: gl.getAttribLocation(program, "a_local"),
            corner: gl.getAttribLocation(program, "a_corner"),
            value: gl.getAttribLocation(program, "a_value"),
            translate: gl.getUniformLocation(program, "u_translate"),
            scale: gl.getUniformLocation(program, "u_scale"),
            viewport: gl.getUniformLocation(program, "u_viewport"),
            radius: gl.getUniformLocation(program, "u_radius"),
            minimum: gl.getUniformLocation(program, "u_minimum"),
            maximum: gl.getUniformLocation(program, "u_maximum"),
            solidCenter: gl.getUniformLocation(program, "u_solid_center")
        };
        gl.disable(gl.DEPTH_TEST);
        gl.disable(gl.CULL_FACE);
        gl.enable(gl.BLEND);
        gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA, gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
        gl.clearColor(0, 0, 0, 0);
    }

    function rebuildHeatmapNodeIndex() {
        heatmap.nodeIndexByRenderId.clear();
        const count = Math.min(
            heatmap.nodeRenderIds.length,
            Math.floor(heatmap.nodeCoordinates.length / 2));
        for (let index = 0; index < count; ++index)
            heatmap.nodeIndexByRenderId.set(heatmap.nodeRenderIds[index], index);
    }

    function uploadHeatmapValues(ids, values, minimum, maximum) {
        heatmap.minimum = Number(minimum);
        heatmap.maximum = Number(maximum);
        const count = Math.min(ids.length, values.length);
        const corners = [
            -1, -1, 1, -1, -1, 1,
            -1, 1, 1, -1, 1, 1
        ];
        const floatsPerVertex = 5;
        const verticesPerNode = 6;
        const data = new Float32Array(count * verticesPerNode * floatsPerVertex);
        let offset = 0;
        let nodeCount = 0;
        for (let index = 0; index < count; ++index) {
            const markerIndex = heatmap.nodeIndexByRenderId.get(ids[index]);
            if (markerIndex === undefined)
                continue;
            const value = values[index];
            if (!Number.isFinite(value))
                continue;
            const localX = heatmap.nodeCoordinates[markerIndex * 2];
            const localY = heatmap.nodeCoordinates[markerIndex * 2 + 1];
            for (let cornerIndex = 0; cornerIndex < corners.length; cornerIndex += 2) {
                data[offset++] = localX;
                data[offset++] = localY;
                data[offset++] = corners[cornerIndex];
                data[offset++] = corners[cornerIndex + 1];
                data[offset++] = value;
            }
            ++nodeCount;
        }
        const gl = heatmap.gl;
        gl.bindBuffer(gl.ARRAY_BUFFER, heatmap.buffer);
        gl.bufferData(gl.ARRAY_BUFFER,
            offset === data.length ? data : data.subarray(0, offset), gl.STATIC_DRAW);
        heatmap.vertexCount = nodeCount * verticesPerNode;
        heatmap.dataReady = true;
    }

    function heatmapPixelRatio(view) {
        const cssRadius = Math.max(1, Number(view.radius) || 1);
        const nodeCount = Math.max(1, heatmap.vertexCount / 6);
        const viewportArea = Math.max(1, view.width * view.height);
        const circleArea = Math.PI * cssRadius * cssRadius;
        const estimatedCssFragmentsPerNode = Math.min(viewportArea, circleArea);
        const budgetRatio = Math.sqrt(
            HEATMAP_TARGET_FRAGMENT_BUDGET
                / Math.max(1, nodeCount * estimatedCssFragmentsPerNode));
        const radiusRatio = HEATMAP_TARGET_RADIUS_PIXELS / cssRadius;
        return Math.max(HEATMAP_MIN_PIXEL_RATIO, Math.min(
            HEATMAP_MAX_PIXEL_RATIO, radiusRatio, budgetRatio));
    }

    function renderHeatmap(view) {
        if (!heatmap.gl || !heatmap.canvas || !heatmap.program || !heatmap.locations)
            throw new Error("Heatmap worker is not initialized");
        const gl = heatmap.gl;
        if (typeof gl.isContextLost === "function" && gl.isContextLost())
            throw new Error("Heatmap WebGL context lost");

        const pixelRatio = heatmapPixelRatio(view);
        const rasterWidth = Math.max(1, Math.ceil(view.width * pixelRatio));
        const rasterHeight = Math.max(1, Math.ceil(view.height * pixelRatio));
        if (heatmap.canvas.width !== rasterWidth)
            heatmap.canvas.width = rasterWidth;
        if (heatmap.canvas.height !== rasterHeight)
            heatmap.canvas.height = rasterHeight;

        gl.viewport(0, 0, rasterWidth, rasterHeight);
        gl.clear(gl.COLOR_BUFFER_BIT);
        if (!heatmap.dataReady || heatmap.vertexCount === 0)
            return;

        const stride = 5 * Float32Array.BYTES_PER_ELEMENT;
        const locations = heatmap.locations;
        gl.useProgram(heatmap.program);
        gl.bindBuffer(gl.ARRAY_BUFFER, heatmap.buffer);
        gl.enableVertexAttribArray(locations.local);
        gl.vertexAttribPointer(locations.local, 2, gl.FLOAT, false, stride, 0);
        gl.enableVertexAttribArray(locations.corner);
        gl.vertexAttribPointer(locations.corner, 2, gl.FLOAT, false, stride, 2 * Float32Array.BYTES_PER_ELEMENT);
        gl.enableVertexAttribArray(locations.value);
        gl.vertexAttribPointer(locations.value, 1, gl.FLOAT, false, stride, 4 * Float32Array.BYTES_PER_ELEMENT);
        gl.uniform2f(locations.translate, view.translateX, view.translateY);
        gl.uniform1f(locations.scale, view.scale);
        gl.uniform2f(locations.viewport, Math.max(1, view.width), Math.max(1, view.height));
        gl.uniform1f(locations.radius, Math.max(1, view.radius));
        gl.uniform1f(locations.minimum, heatmap.minimum);
        gl.uniform1f(locations.maximum, heatmap.maximum);
        gl.uniform1f(locations.solidCenter, Math.max(0, Math.min(0.9, view.solidCenter / 100)));
        gl.drawArrays(gl.TRIANGLES, 0, heatmap.vertexCount);
    }

    async function handleNetworkMessage(message) {
        if (message.type === "network-geometry") {
            network.markerCoordinates = message.markerCoordinates || new Float64Array(0);
            network.markerMetadata = message.markerMetadata || new Uint32Array(0);
            network.pipeCoordinates = message.pipeCoordinates || new Float64Array(0);
            network.pipeRenderIds = message.pipeRenderIds || new Uint32Array(0);
            network.deviceCoordinates = message.deviceCoordinates || new Float64Array(0);
            network.deviceRenderIds = message.deviceRenderIds || new Uint32Array(0);
            network.geometryOriginX = Number(message.geometryOriginX) || 0;
            network.geometryOriginY = Number(message.geometryOriginY) || 0;
            return;
        }
        if (message.type === "network-icon") {
            const entityType = Number(message.entityType) | 0;
            const bitmap = message.bitmap;
            if (bitmap) {
                const previous = network.icons.get(entityType);
                if (previous && typeof previous.close === "function")
                    previous.close();
                network.icons.set(entityType, bitmap);
                network.tintedIcons.clear();
            }
            return;
        }
        if (message.type === "network-symbology") {
            network.nodeVisual = Number(message.nodeVisual) | 0;
            network.nodeSizePercent = Number(message.nodeSizePercent) || 100;
            network.iconSizePercent = Number(message.iconSizePercent) || 100;
            network.nodeMinimum = Number(message.nodeMinimum);
            network.nodeMaximum = Number(message.nodeMaximum);
            network.nodeValues = valuesFromTypedArrays(
                message.nodeValueIds || new Uint32Array(0),
                message.nodeValues || new Float64Array(0));
            network.linkVisual = Number(message.linkVisual) | 0;
            network.linkThicknessPixels = Number(message.linkThicknessPixels) || 3;
            network.linkMinimum = Number(message.linkMinimum);
            network.linkMaximum = Number(message.linkMaximum);
            network.linkValues = valuesFromTypedArrays(
                message.linkValueIds || new Uint32Array(0),
                message.linkValues || new Float64Array(0));
            network.tintedIcons.clear();
            return;
        }
        if (message.type === "network-render") {
            try {
                const bitmap = await renderNetwork(message);
                self.postMessage({
                    type: "network-rendered",
                    requestId: message.requestId,
                    bitmap: bitmap,
                    zoom: message.zoom,
                    originX: message.originX,
                    originY: message.originY,
                    width: message.width,
                    height: message.height,
                    fallback: message.fallback,
                    styleRevision: message.styleRevision
                }, [bitmap]);
            } catch (error) {
                self.postMessage({
                    type: "network-error",
                    requestId: message.requestId,
                    message: error && error.message ? error.message : String(error)
                });
            }
        }
    }

    function handleHeatmapMessage(message) {
        if (message.type === "heatmap-geometry") {
            heatmap.nodeCoordinates = message.nodeCoordinates || new Float64Array(0);
            heatmap.nodeRenderIds = message.nodeRenderIds || new Uint32Array(0);
            rebuildHeatmapNodeIndex();
            heatmap.dataReady = false;
            return;
        }
        if (message.type === "heatmap-values") {
            uploadHeatmapValues(
                message.renderIds || new Uint32Array(0),
                message.values || new Float64Array(0),
                message.minimum,
                message.maximum);
            return;
        }
        if (message.type === "heatmap-clear") {
            heatmap.vertexCount = 0;
            heatmap.dataReady = false;
            if (heatmap.gl) {
                heatmap.gl.clearColor(0, 0, 0, 0);
                heatmap.gl.clear(heatmap.gl.COLOR_BUFFER_BIT);
            }
            return;
        }
        if (message.type === "heatmap-view") {
            try {
                renderHeatmap(message);
                self.postMessage({ type: "heatmap-rendered", requestId: message.requestId });
            } catch (error) {
                self.postMessage({
                    type: "heatmap-error",
                    requestId: message.requestId,
                    message: error && error.message ? error.message : String(error)
                });
            }
        }
    }

    self.onmessage = function (event) {
        const message = event.data || {};
        if (message.type === "init-network") {
            role = "network";
            network.iconsReady = loadNetworkIcons(message.iconBaseUrl || self.location.href);
            self.postMessage({ type: "network-ready" });
            return;
        }
        if (message.type === "init-heatmap") {
            role = "heatmap";
            try {
                initializeHeatmapWebGl(message.canvas);
                self.postMessage({ type: "heatmap-ready" });
            } catch (error) {
                self.postMessage({
                    type: "heatmap-error",
                    requestId: 0,
                    message: error && error.message ? error.message : String(error)
                });
            }
            return;
        }
        if (role === "network") {
            handleNetworkMessage(message);
            return;
        }
        if (role === "heatmap")
            handleHeatmapMessage(message);
    };
}());
