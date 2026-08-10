(function () {
    "use strict";

    const REFERENCE_ZOOM = 18;
    const ENTITY_JUNCTION = 1;
    const ENTITY_RESERVOIR = 2;
    const ENTITY_TANK = 3;
    const ENTITY_PIPE = 4;
    const ENTITY_PUMP = 5;
    const ENTITY_VALVE = 6;

    class VectorDocument {
        constructor() {
            this.commands = [];
        }

        addStroke(path, color, width, lineCap = "round", lineJoin = "round") {
            if (!path || !color || !(width > 0))
                return;
            this.commands.push({
                type: "stroke",
                path: path,
                color: color,
                width: width,
                lineCap: lineCap,
                lineJoin: lineJoin
            });
        }

        addFill(path, color) {
            if (!path || !color)
                return;
            this.commands.push({ type: "fill", path: path, color: color });
        }

        addImage(image, x, y, width, height) {
            if (!image || !(width > 0) || !(height > 0))
                return;
            this.commands.push({
                type: "image",
                image: image,
                x: x,
                y: y,
                width: width,
                height: height
            });
        }

        paint(context) {
            if (!context)
                return;

            context.save();
            for (const command of this.commands) {
                if (command.type === "stroke") {
                    context.strokeStyle = command.color;
                    context.lineWidth = command.width;
                    context.lineCap = command.lineCap;
                    context.lineJoin = command.lineJoin;
                    context.stroke(command.path);
                } else if (command.type === "fill") {
                    context.fillStyle = command.color;
                    context.fill(command.path);
                } else if (command.type === "image") {
                    context.drawImage(
                        command.image,
                        command.x,
                        command.y,
                        command.width,
                        command.height);
                }
            }
            context.restore();
        }
    }

    class RetainedCanvasLayer {
        constructor(zIndex) {
            this.canvas = createCanvas(zIndex);
            this.frameRequest = 0;
        }

        attach(parent) {
            if (parent && this.canvas.parentElement !== parent)
                parent.appendChild(this.canvas);
        }

        configure(width, height, ratio) {
            return configureCanvas(this.canvas, width, height, ratio);
        }

        render(documentVector, width, height, ratio) {
            const context = this.configure(width, height, ratio);
            if (!context)
                return false;
            if (documentVector)
                documentVector.paint(context);
            return true;
        }

        schedule(callback) {
            if (this.frameRequest !== 0)
                return;
            this.frameRequest = window.requestAnimationFrame(() => {
                this.frameRequest = 0;
                callback();
            });
        }

        cancelScheduled() {
            if (this.frameRequest === 0)
                return;
            window.cancelAnimationFrame(this.frameRequest);
            this.frameRequest = 0;
        }

        hide() {
            this.canvas.style.display = "none";
        }

        show() {
            this.canvas.style.display = "block";
        }

        clear() {
            this.cancelScheduled();
            this.canvas.style.display = "none";
            this.canvas.style.transform = "";
            const context = this.canvas.getContext("2d");
            if (context)
                context.clearRect(0, 0, this.canvas.width, this.canvas.height);
        }

        destroy() {
            this.cancelScheduled();
            this.canvas.remove();
        }
    }

    function createPath() {
        return new Path2D();
    }

    function addPolyline(path, vertices, pointFunction) {
        if (!path || !Array.isArray(vertices) || vertices.length < 2)
            return;

        const first = pointFunction(vertices[0]);
        path.moveTo(first.x, first.y);
        for (let index = 1; index < vertices.length; ++index) {
            const point = pointFunction(vertices[index]);
            path.lineTo(point.x, point.y);
        }
    }

    function addCircle(path, x, y, radius) {
        if (!path || !(radius > 0))
            return;
        path.moveTo(x + radius, y);
        path.arc(x, y, radius, 0, Math.PI * 2);
    }

    function createCanvas(zIndex) {
        const canvas = document.createElement("canvas");
        canvas.setAttribute("aria-hidden", "true");
        canvas.style.position = "absolute";
        canvas.style.left = "0";
        canvas.style.top = "0";
        canvas.style.pointerEvents = "none";
        canvas.style.transformOrigin = "0 0";
        if (zIndex !== undefined && zIndex !== null)
            canvas.style.zIndex = String(zIndex);
        return canvas;
    }

    function configureCanvas(canvas, width, height, ratio) {
        if (!canvas)
            return null;

        const effectiveRatio = Math.max(0.5, Number(ratio) || 1);
        const cssWidth = Math.max(1, Number(width) || 1);
        const cssHeight = Math.max(1, Number(height) || 1);
        const physicalWidth = Math.max(1, Math.ceil(cssWidth * effectiveRatio));
        const physicalHeight = Math.max(1, Math.ceil(cssHeight * effectiveRatio));
        if (canvas.width !== physicalWidth)
            canvas.width = physicalWidth;
        if (canvas.height !== physicalHeight)
            canvas.height = physicalHeight;
        const cssWidthText = `${cssWidth}px`;
        const cssHeightText = `${cssHeight}px`;
        if (canvas.style.width !== cssWidthText)
            canvas.style.width = cssWidthText;
        if (canvas.style.height !== cssHeightText)
            canvas.style.height = cssHeightText;

        const context = canvas.getContext("2d");
        if (!context)
            return null;
        context.setTransform(effectiveRatio, 0, 0, effectiveRatio, 0, 0);
        context.clearRect(0, 0, cssWidth, cssHeight);
        context.imageSmoothingEnabled = true;
        return context;
    }

    function normalizeUuid(value) {
        return typeof value === "string" ? value.toLowerCase() : "";
    }

    function projectNetworkSnapshot(snapshot, options) {
        if (!snapshot || !Array.isArray(snapshot.nodes) || !Array.isArray(snapshot.links))
            return null;
        if (!options || typeof options.longitudeToWorldPixel !== "function"
            || typeof options.latitudeToWorldPixel !== "function"
            || typeof options.nearestWrappedWorldPixel !== "function") {
            throw new TypeError("Invalid AOWIS network projection options");
        }

        const longitudeToWorldPixel = options.longitudeToWorldPixel;
        const latitudeToWorldPixel = options.latitudeToWorldPixel;
        const nearestWrappedWorldPixel = options.nearestWrappedWorldPixel;
        const configuredAnchor = Number(options.anchorX);
        let anchorX = Number.isFinite(configuredAnchor) ? configuredAnchor : null;
        let minimumX = Number.POSITIVE_INFINITY;
        let minimumY = Number.POSITIVE_INFINITY;
        let maximumX = Number.NEGATIVE_INFINITY;
        let maximumY = Number.NEGATIVE_INFINITY;
        const nodes = [];
        const links = [];
        const nodesByUuid = new Map();
        const nodesByRenderId = new Map();

        function includePoint(point) {
            minimumX = Math.min(minimumX, point.x);
            minimumY = Math.min(minimumY, point.y);
            maximumX = Math.max(maximumX, point.x);
            maximumY = Math.max(maximumY, point.y);
        }

        for (const rawNode of snapshot.nodes) {
            if (!Array.isArray(rawNode) || rawNode.length < 6)
                continue;
            const longitude = Number(rawNode[4]);
            const latitude = Number(rawNode[5]);
            if (!Number.isFinite(longitude) || !Number.isFinite(latitude))
                continue;

            const rawX = longitudeToWorldPixel(longitude);
            if (anchorX === null)
                anchorX = rawX;
            const node = {
                renderId: Number(rawNode[0]) >>> 0,
                entityType: Number(rawNode[1]) | 0,
                uuid: normalizeUuid(rawNode[2]),
                id: String(rawNode[3] || ""),
                longitude: longitude,
                latitude: latitude,
                x: nearestWrappedWorldPixel(rawX, anchorX),
                y: latitudeToWorldPixel(latitude)
            };
            nodes.push(node);
            nodesByRenderId.set(node.renderId, node);
            if (node.uuid)
                nodesByUuid.set(node.uuid, node);
            includePoint(node);
        }

        for (const rawLink of snapshot.links) {
            if (!Array.isArray(rawLink) || rawLink.length < 7 || !Array.isArray(rawLink[6]))
                continue;

            const vertices = [];
            const startNodeRenderId = Number(rawLink[4]) >>> 0;
            const startNode = nodesByRenderId.get(startNodeRenderId);
            let previousX = startNode ? startNode.x : anchorX;
            for (const rawVertex of rawLink[6]) {
                if (!Array.isArray(rawVertex) || rawVertex.length < 2)
                    continue;
                const longitude = Number(rawVertex[0]);
                const latitude = Number(rawVertex[1]);
                if (!Number.isFinite(longitude) || !Number.isFinite(latitude))
                    continue;
                const rawX = longitudeToWorldPixel(longitude);
                if (previousX === null)
                    previousX = rawX;
                const point = {
                    longitude: longitude,
                    latitude: latitude,
                    x: nearestWrappedWorldPixel(rawX, previousX),
                    y: latitudeToWorldPixel(latitude)
                };
                vertices.push(point);
                includePoint(point);
                previousX = point.x;
            }

            if (vertices.length < 2)
                continue;
            links.push({
                renderId: Number(rawLink[0]) >>> 0,
                entityType: Number(rawLink[1]) | 0,
                uuid: normalizeUuid(rawLink[2]),
                id: String(rawLink[3] || ""),
                startNodeRenderId: startNodeRenderId,
                endNodeRenderId: Number(rawLink[5]) >>> 0,
                vertices: vertices
            });
        }

        const ready = Number.isFinite(minimumX) && Number.isFinite(minimumY);
        return {
            nodes: nodes,
            links: links,
            nodesByUuid: nodesByUuid,
            nodesByRenderId: nodesByRenderId,
            anchorX: anchorX === null ? 0 : anchorX,
            minimumX: ready ? minimumX : 0,
            minimumY: ready ? minimumY : 0,
            maximumX: ready ? maximumX : 0,
            maximumY: ready ? maximumY : 0,
            originX: ready ? (minimumX + maximumX) / 2 : 0,
            originY: ready ? (minimumY + maximumY) / 2 : 0,
            ready: ready && (nodes.length > 0 || links.length > 0)
        };
    }

    function polylineMidpoint(vertices) {
        if (!Array.isArray(vertices) || vertices.length === 0)
            return null;
        if (vertices.length === 1)
            return vertices[0];

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

    window.aowisBrowserVector = {
        REFERENCE_ZOOM: REFERENCE_ZOOM,
        ENTITY_JUNCTION: ENTITY_JUNCTION,
        ENTITY_RESERVOIR: ENTITY_RESERVOIR,
        ENTITY_TANK: ENTITY_TANK,
        ENTITY_PIPE: ENTITY_PIPE,
        ENTITY_PUMP: ENTITY_PUMP,
        ENTITY_VALVE: ENTITY_VALVE,
        VectorDocument: VectorDocument,
        RetainedCanvasLayer: RetainedCanvasLayer,
        createPath: createPath,
        addPolyline: addPolyline,
        addCircle: addCircle,
        createCanvas: createCanvas,
        configureCanvas: configureCanvas,
        normalizeUuid: normalizeUuid,
        projectNetworkSnapshot: projectNetworkSnapshot,
        polylineMidpoint: polylineMidpoint
    };
})();
