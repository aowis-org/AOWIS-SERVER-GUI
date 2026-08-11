(function () {
    "use strict";

    const REFERENCE_ZOOM = 18;
    const ENTITY_JUNCTION = 1;
    const ENTITY_RESERVOIR = 2;
    const ENTITY_TANK = 3;
    const ENTITY_PIPE = 4;
    const ENTITY_PUMP = 5;
    const ENTITY_VALVE = 6;

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
        createCanvas: createCanvas,
        normalizeUuid: normalizeUuid,
        projectNetworkSnapshot: projectNetworkSnapshot,
        polylineMidpoint: polylineMidpoint
    };
})();
