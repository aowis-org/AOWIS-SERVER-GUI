(function () {
    "use strict";

    const ICON_ATLAS_CELL_SIZE = 256;
    const ICON_ATLAS_GRID_SIZE = 2;
    const ICON_ATLAS_PADDING = 16;
    const ICON_ATLAS_SIZE = ICON_ATLAS_CELL_SIZE * ICON_ATLAS_GRID_SIZE;
    const ICON_ATLAS_INSET = ICON_ATLAS_PADDING / ICON_ATLAS_SIZE;
    const FLOAT_SIZE = Float32Array.BYTES_PER_ELEMENT;

    const SEGMENT_VERTEX_SHADER = [
        "#version 300 es",
        "precision highp float;",
        "in vec2 a_start;",
        "in vec2 a_end;",
        "in vec3 a_color;",
        "uniform vec2 u_translate;",
        "uniform float u_scale;",
        "uniform vec2 u_viewport;",
        "uniform float u_half_width;",
        "out vec2 v_screen;",
        "flat out vec2 v_start;",
        "flat out vec2 v_end;",
        "flat out vec3 v_color;",
        "const vec2 CORNERS[6] = vec2[6](",
        "    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0),",
        "    vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0, 1.0));",
        "void main() {",
        "    vec2 start_screen = u_translate + a_start * u_scale;",
        "    vec2 end_screen = u_translate + a_end * u_scale;",
        "    vec2 delta = end_screen - start_screen;",
        "    float segment_length = length(delta);",
        "    vec2 direction = segment_length > 0.0001",
        "        ? delta / segment_length : vec2(1.0, 0.0);",
        "    vec2 normal = vec2(-direction.y, direction.x);",
        "    vec2 corner = CORNERS[gl_VertexID];",
        "    vec2 base = corner.x < 0.0 ? start_screen : end_screen;",
        "    vec2 screen = base + direction * corner.x * u_half_width",
        "        + normal * corner.y * u_half_width;",
        "    vec2 clip = vec2(screen.x / u_viewport.x * 2.0 - 1.0,",
        "        1.0 - screen.y / u_viewport.y * 2.0);",
        "    gl_Position = vec4(clip, 0.0, 1.0);",
        "    v_screen = screen;",
        "    v_start = start_screen;",
        "    v_end = end_screen;",
        "    v_color = a_color;",
        "}"
    ].join("\n");

    const SEGMENT_FRAGMENT_SHADER = [
        "#version 300 es",
        "precision highp float;",
        "in vec2 v_screen;",
        "flat in vec2 v_start;",
        "flat in vec2 v_end;",
        "flat in vec3 v_color;",
        "uniform float u_half_width;",
        "uniform float u_pixel_ratio;",
        "out vec4 out_color;",
        "void main() {",
        "    vec2 segment = v_end - v_start;",
        "    float length_squared = dot(segment, segment);",
        "    float projection = length_squared > 0.0001",
        "        ? clamp(dot(v_screen - v_start, segment) / length_squared, 0.0, 1.0)",
        "        : 0.0;",
        "    vec2 nearest = v_start + segment * projection;",
        "    float distance_from_segment = length(v_screen - nearest);",
        "    float edge = max(fwidth(distance_from_segment),",
        "        0.5 / max(1.0, u_pixel_ratio));",
        "    float alpha = 1.0 - smoothstep(",
        "        u_half_width - edge, u_half_width + edge, distance_from_segment);",
        "    if (alpha <= 0.0)",
        "        discard;",
        "    out_color = vec4(v_color, alpha);",
        "}"
    ].join("\n");

    const JUNCTION_VERTEX_SHADER = [
        "#version 300 es",
        "precision highp float;",
        "in vec2 a_center;",
        "in vec3 a_color;",
        "uniform vec2 u_translate;",
        "uniform float u_scale;",
        "uniform vec2 u_viewport;",
        "uniform float u_radius;",
        "out vec2 v_corner;",
        "flat out vec3 v_color;",
        "const vec2 CORNERS[6] = vec2[6](",
        "    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0),",
        "    vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0, 1.0));",
        "void main() {",
        "    vec2 corner = CORNERS[gl_VertexID];",
        "    vec2 screen = u_translate + a_center * u_scale + corner * u_radius;",
        "    vec2 clip = vec2(screen.x / u_viewport.x * 2.0 - 1.0,",
        "        1.0 - screen.y / u_viewport.y * 2.0);",
        "    gl_Position = vec4(clip, 0.0, 1.0);",
        "    v_corner = corner;",
        "    v_color = a_color;",
        "}"
    ].join("\n");

    const JUNCTION_FRAGMENT_SHADER = [
        "#version 300 es",
        "precision highp float;",
        "in vec2 v_corner;",
        "flat in vec3 v_color;",
        "uniform float u_radius;",
        "uniform float u_pixel_ratio;",
        "out vec4 out_color;",
        "void main() {",
        "    float distance_from_center = length(v_corner);",
        "    float edge = max(fwidth(distance_from_center),",
        "        0.5 / max(1.0, u_radius * u_pixel_ratio));",
        "    float alpha = 1.0 - smoothstep(1.0 - edge, 1.0 + edge, distance_from_center);",
        "    if (alpha <= 0.0)",
        "        discard;",
        "    out_color = vec4(v_color, alpha);",
        "}"
    ].join("\n");

    const ICON_VERTEX_SHADER = [
        "#version 300 es",
        "precision highp float;",
        "in vec2 a_center;",
        "in vec2 a_aspect;",
        "in float a_slot;",
        "in vec3 a_color;",
        "uniform vec2 u_translate;",
        "uniform float u_scale;",
        "uniform vec2 u_viewport;",
        "uniform float u_half_size;",
        "out vec2 v_uv;",
        "flat out vec3 v_color;",
        "const float GRID_SIZE = " + ICON_ATLAS_GRID_SIZE.toFixed(1) + ";",
        "const float ATLAS_INSET = " + ICON_ATLAS_INSET.toFixed(8) + ";",
        "const vec2 CORNERS[6] = vec2[6](",
        "    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0),",
        "    vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0, 1.0));",
        "void main() {",
        "    vec2 corner = CORNERS[gl_VertexID];",
        "    vec2 screen = u_translate + a_center * u_scale",
        "        + corner * a_aspect * u_half_size;",
        "    vec2 clip = vec2(screen.x / u_viewport.x * 2.0 - 1.0,",
        "        1.0 - screen.y / u_viewport.y * 2.0);",
        "    gl_Position = vec4(clip, 0.0, 1.0);",
        "    float column = mod(a_slot, GRID_SIZE);",
        "    float row = floor(a_slot / GRID_SIZE);",
        "    vec2 cell_origin = vec2(column, row) / GRID_SIZE;",
        "    vec2 uv_minimum = cell_origin + vec2(ATLAS_INSET);",
        "    vec2 uv_maximum = cell_origin + vec2(1.0 / GRID_SIZE - ATLAS_INSET);",
        "    v_uv = mix(uv_minimum, uv_maximum, corner * 0.5 + 0.5);",
        "    v_color = a_color;",
        "}"
    ].join("\n");

    const ICON_FRAGMENT_SHADER = [
        "#version 300 es",
        "precision highp float;",
        "in vec2 v_uv;",
        "flat in vec3 v_color;",
        "uniform sampler2D u_atlas;",
        "out vec4 out_color;",
        "void main() {",
        "    float alpha = texture(u_atlas, v_uv).a;",
        "    if (alpha <= 0.001)",
        "        discard;",
        "    out_color = vec4(v_color, alpha);",
        "}"
    ].join("\n");

    function compileShader(gl, type, source, label) {
        const shader = gl.createShader(type);
        if (!shader)
            throw new Error("Unable to create the AOWIS " + label + " shader");

        gl.shaderSource(shader, source);
        gl.compileShader(shader);
        if (gl.getShaderParameter(shader, gl.COMPILE_STATUS))
            return shader;

        const details = gl.getShaderInfoLog(shader) || "unknown compilation error";
        gl.deleteShader(shader);
        throw new Error("AOWIS " + label + " shader compilation failed: " + details);
    }

    function createProgram(gl, vertexSource, fragmentSource, label) {
        const vertexShader = compileShader(
            gl, gl.VERTEX_SHADER, vertexSource, label + " vertex");
        let fragmentShader = null;
        let program = null;
        try {
            fragmentShader = compileShader(
                gl, gl.FRAGMENT_SHADER, fragmentSource, label + " fragment");
            program = gl.createProgram();
            if (!program)
                throw new Error("Unable to create the AOWIS " + label + " program");

            gl.attachShader(program, vertexShader);
            gl.attachShader(program, fragmentShader);
            gl.linkProgram(program);
            if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
                const details = gl.getProgramInfoLog(program) || "unknown link error";
                throw new Error("AOWIS " + label + " program linking failed: " + details);
            }
            return program;
        } catch (error) {
            if (program)
                gl.deleteProgram(program);
            throw error;
        } finally {
            gl.deleteShader(vertexShader);
            if (fragmentShader)
                gl.deleteShader(fragmentShader);
        }
    }

    function attributeLocation(gl, program, name) {
        const location = gl.getAttribLocation(program, name);
        if (location < 0)
            throw new Error("AOWIS WebGL attribute is unavailable: " + name);
        return location;
    }

    function uniformLocation(gl, program, name) {
        const location = gl.getUniformLocation(program, name);
        if (location === null)
            throw new Error("AOWIS WebGL uniform is unavailable: " + name);
        return location;
    }

    function createBuffer(gl) {
        const buffer = gl.createBuffer();
        if (!buffer)
            throw new Error("Unable to create an AOWIS WebGL buffer");
        return buffer;
    }

    function createVertexArray(gl) {
        const vertexArray = gl.createVertexArray();
        if (!vertexArray)
            throw new Error("Unable to create an AOWIS WebGL vertex array");
        return vertexArray;
    }

    function createTexture(gl) {
        const texture = gl.createTexture();
        if (!texture)
            throw new Error("Unable to create the AOWIS network icon texture");
        return texture;
    }

    function configureInstanceAttribute(gl, location, size, stride, offset) {
        gl.enableVertexAttribArray(location);
        gl.vertexAttribPointer(location, size, gl.FLOAT, false, stride, offset);
        gl.vertexAttribDivisor(location, 1);
    }

    function disposeResources(gl, resources) {
        if (!gl || !resources)
            return;

        for (const vertexArray of resources.vertexArrays)
            gl.deleteVertexArray(vertexArray);
        for (const buffer of resources.buffers)
            gl.deleteBuffer(buffer);
        if (resources.iconTexture)
            gl.deleteTexture(resources.iconTexture);
        for (const program of resources.programs)
            gl.deleteProgram(program);
    }

    function createResources(gl) {
        const resources = {
            programs: [],
            buffers: [],
            vertexArrays: [],
            iconTexture: null,
            geometryRevision: 0,
            colorRevision: 0,
            iconRevision: 0,
            segmentCount: 0,
            junctionCount: 0,
            iconCount: 0,
            segmentColorLength: -1,
            junctionColorLength: -1,
            iconColorLength: -1
        };

        try {
            const segmentHandle = createProgram(
                gl, SEGMENT_VERTEX_SHADER, SEGMENT_FRAGMENT_SHADER, "network segment");
            resources.programs.push(segmentHandle);
            const junctionHandle = createProgram(
                gl, JUNCTION_VERTEX_SHADER, JUNCTION_FRAGMENT_SHADER, "network junction");
            resources.programs.push(junctionHandle);
            const iconHandle = createProgram(
                gl, ICON_VERTEX_SHADER, ICON_FRAGMENT_SHADER, "network icon");
            resources.programs.push(iconHandle);

            resources.segmentProgram = {
                handle: segmentHandle,
                start: attributeLocation(gl, segmentHandle, "a_start"),
                end: attributeLocation(gl, segmentHandle, "a_end"),
                color: attributeLocation(gl, segmentHandle, "a_color"),
                translate: uniformLocation(gl, segmentHandle, "u_translate"),
                scale: uniformLocation(gl, segmentHandle, "u_scale"),
                viewport: uniformLocation(gl, segmentHandle, "u_viewport"),
                halfWidth: uniformLocation(gl, segmentHandle, "u_half_width"),
                pixelRatio: uniformLocation(gl, segmentHandle, "u_pixel_ratio")
            };
            resources.junctionProgram = {
                handle: junctionHandle,
                center: attributeLocation(gl, junctionHandle, "a_center"),
                color: attributeLocation(gl, junctionHandle, "a_color"),
                translate: uniformLocation(gl, junctionHandle, "u_translate"),
                scale: uniformLocation(gl, junctionHandle, "u_scale"),
                viewport: uniformLocation(gl, junctionHandle, "u_viewport"),
                radius: uniformLocation(gl, junctionHandle, "u_radius"),
                pixelRatio: uniformLocation(gl, junctionHandle, "u_pixel_ratio")
            };
            resources.iconProgram = {
                handle: iconHandle,
                center: attributeLocation(gl, iconHandle, "a_center"),
                aspect: attributeLocation(gl, iconHandle, "a_aspect"),
                slot: attributeLocation(gl, iconHandle, "a_slot"),
                color: attributeLocation(gl, iconHandle, "a_color"),
                translate: uniformLocation(gl, iconHandle, "u_translate"),
                scale: uniformLocation(gl, iconHandle, "u_scale"),
                viewport: uniformLocation(gl, iconHandle, "u_viewport"),
                halfSize: uniformLocation(gl, iconHandle, "u_half_size"),
                atlas: uniformLocation(gl, iconHandle, "u_atlas")
            };

            resources.segmentGeometryBuffer = createBuffer(gl);
            resources.segmentColorBuffer = createBuffer(gl);
            resources.junctionGeometryBuffer = createBuffer(gl);
            resources.junctionColorBuffer = createBuffer(gl);
            resources.iconGeometryBuffer = createBuffer(gl);
            resources.iconColorBuffer = createBuffer(gl);
            resources.buffers.push(
                resources.segmentGeometryBuffer,
                resources.segmentColorBuffer,
                resources.junctionGeometryBuffer,
                resources.junctionColorBuffer,
                resources.iconGeometryBuffer,
                resources.iconColorBuffer);

            resources.segmentVertexArray = createVertexArray(gl);
            resources.junctionVertexArray = createVertexArray(gl);
            resources.iconVertexArray = createVertexArray(gl);
            resources.vertexArrays.push(
                resources.segmentVertexArray,
                resources.junctionVertexArray,
                resources.iconVertexArray);

            gl.bindVertexArray(resources.segmentVertexArray);
            gl.bindBuffer(gl.ARRAY_BUFFER, resources.segmentGeometryBuffer);
            configureInstanceAttribute(
                gl, resources.segmentProgram.start, 2, 4 * FLOAT_SIZE, 0);
            configureInstanceAttribute(
                gl, resources.segmentProgram.end, 2, 4 * FLOAT_SIZE, 2 * FLOAT_SIZE);
            gl.bindBuffer(gl.ARRAY_BUFFER, resources.segmentColorBuffer);
            configureInstanceAttribute(
                gl, resources.segmentProgram.color, 3, 3 * FLOAT_SIZE, 0);

            gl.bindVertexArray(resources.junctionVertexArray);
            gl.bindBuffer(gl.ARRAY_BUFFER, resources.junctionGeometryBuffer);
            configureInstanceAttribute(
                gl, resources.junctionProgram.center, 2, 2 * FLOAT_SIZE, 0);
            gl.bindBuffer(gl.ARRAY_BUFFER, resources.junctionColorBuffer);
            configureInstanceAttribute(
                gl, resources.junctionProgram.color, 3, 3 * FLOAT_SIZE, 0);

            gl.bindVertexArray(resources.iconVertexArray);
            gl.bindBuffer(gl.ARRAY_BUFFER, resources.iconGeometryBuffer);
            configureInstanceAttribute(
                gl, resources.iconProgram.center, 2, 5 * FLOAT_SIZE, 0);
            configureInstanceAttribute(
                gl, resources.iconProgram.aspect, 2, 5 * FLOAT_SIZE, 2 * FLOAT_SIZE);
            configureInstanceAttribute(
                gl, resources.iconProgram.slot, 1, 5 * FLOAT_SIZE, 4 * FLOAT_SIZE);
            gl.bindBuffer(gl.ARRAY_BUFFER, resources.iconColorBuffer);
            configureInstanceAttribute(
                gl, resources.iconProgram.color, 3, 3 * FLOAT_SIZE, 0);

            resources.iconTexture = createTexture(gl);
            gl.bindTexture(gl.TEXTURE_2D, resources.iconTexture);
            gl.texParameteri(
                gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
            gl.texImage2D(
                gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0,
                gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array([0, 0, 0, 0]));

            gl.bindTexture(gl.TEXTURE_2D, null);
            gl.bindBuffer(gl.ARRAY_BUFFER, null);
            gl.bindVertexArray(null);
            return resources;
        } catch (error) {
            gl.bindTexture(gl.TEXTURE_2D, null);
            gl.bindBuffer(gl.ARRAY_BUFFER, null);
            gl.bindVertexArray(null);
            disposeResources(gl, resources);
            throw error;
        }
    }

    function floatData(value, tupleSize, name) {
        const data = value instanceof Float32Array
            ? value : new Float32Array(value || []);
        if (data.length % tupleSize !== 0)
            throw new TypeError("Invalid AOWIS WebGL " + name + " data");
        return data;
    }

    function uploadStaticBuffer(gl, buffer, data) {
        gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
        gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
    }

    function uploadColorBuffer(gl, buffer, data, previousLength) {
        gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
        if (previousLength === data.length)
            gl.bufferSubData(gl.ARRAY_BUFFER, 0, data);
        else
            gl.bufferData(gl.ARRAY_BUFFER, data, gl.DYNAMIC_DRAW);
        return data.length;
    }

    class NetworkWebGlRenderer {
        constructor(canvas, callbacks) {
            if (!canvas || typeof canvas.getContext !== "function")
                throw new TypeError("Invalid AOWIS network WebGL canvas");

            this.canvas = canvas;
            this.callbacks = callbacks || {};
            this.gl = canvas.getContext("webgl2", {
                alpha: true,
                antialias: false,
                depth: false,
                stencil: false,
                premultipliedAlpha: false,
                preserveDrawingBuffer: false,
                powerPreference: "high-performance"
            });
            if (!this.gl)
                throw new Error("WebGL2 is unavailable");

            this.resources = createResources(this.gl);
            this.segmentGeometry = new Float32Array(0);
            this.junctionGeometry = new Float32Array(0);
            this.iconGeometry = new Float32Array(0);
            this.segmentColors = new Float32Array(0);
            this.junctionColors = new Float32Array(0);
            this.iconColors = new Float32Array(0);
            this.iconImages = [];
            this.geometryRevision = 1;
            this.colorRevision = 1;
            this.iconRevision = 1;
            this.contextLost = false;
            this.destroyed = false;
            this.contextLostHandler = (event) => this.handleContextLost(event);
            this.contextRestoredHandler = () => this.handleContextRestored();
            canvas.addEventListener("webglcontextlost", this.contextLostHandler, false);
            canvas.addEventListener("webglcontextrestored", this.contextRestoredHandler, false);
        }

        setGeometry(geometry) {
            const source = geometry || {};
            this.segmentGeometry = floatData(source.segments, 4, "segment geometry");
            this.junctionGeometry = floatData(source.junctions, 2, "junction geometry");
            this.iconGeometry = floatData(source.icons, 5, "icon geometry");
            ++this.geometryRevision;
        }

        setColors(colors) {
            const source = colors || {};
            this.segmentColors = floatData(source.segments, 3, "segment color");
            this.junctionColors = floatData(source.junctions, 3, "junction color");
            this.iconColors = floatData(source.icons, 3, "icon color");
            ++this.colorRevision;
        }

        setIconImages(iconImages) {
            this.iconImages = Array.isArray(iconImages)
                ? iconImages.slice() : [];
            ++this.iconRevision;
        }

        handleContextLost(event) {
            event.preventDefault();
            this.contextLost = true;
            this.resources = null;
            this.canvas.style.display = "none";
            if (typeof this.callbacks.contextLost === "function")
                this.callbacks.contextLost();
        }

        handleContextRestored() {
            if (this.destroyed)
                return;

            try {
                this.resources = createResources(this.gl);
                this.contextLost = false;
                if (typeof this.callbacks.contextRestored === "function")
                    this.callbacks.contextRestored();
            } catch (error) {
                this.contextLost = true;
                if (typeof this.callbacks.error === "function")
                    this.callbacks.error(error);
            }
        }

        uploadGeometry() {
            const resources = this.resources;
            if (resources.geometryRevision === this.geometryRevision)
                return;

            uploadStaticBuffer(
                this.gl, resources.segmentGeometryBuffer, this.segmentGeometry);
            uploadStaticBuffer(
                this.gl, resources.junctionGeometryBuffer, this.junctionGeometry);
            uploadStaticBuffer(
                this.gl, resources.iconGeometryBuffer, this.iconGeometry);
            resources.segmentCount = this.segmentGeometry.length / 4;
            resources.junctionCount = this.junctionGeometry.length / 2;
            resources.iconCount = this.iconGeometry.length / 5;
            resources.geometryRevision = this.geometryRevision;
            resources.colorRevision = 0;
        }

        uploadColors() {
            const resources = this.resources;
            if (resources.colorRevision === this.colorRevision)
                return;
            if (this.segmentColors.length !== resources.segmentCount * 3
                || this.junctionColors.length !== resources.junctionCount * 3
                || this.iconColors.length !== resources.iconCount * 3) {
                throw new Error("AOWIS network WebGL geometry and color counts differ");
            }

            resources.segmentColorLength = uploadColorBuffer(
                this.gl,
                resources.segmentColorBuffer,
                this.segmentColors,
                resources.segmentColorLength);
            resources.junctionColorLength = uploadColorBuffer(
                this.gl,
                resources.junctionColorBuffer,
                this.junctionColors,
                resources.junctionColorLength);
            resources.iconColorLength = uploadColorBuffer(
                this.gl,
                resources.iconColorBuffer,
                this.iconColors,
                resources.iconColorLength);
            resources.colorRevision = this.colorRevision;
        }

        uploadIconAtlas() {
            const resources = this.resources;
            if (resources.iconRevision === this.iconRevision)
                return;

            const atlas = document.createElement("canvas");
            atlas.width = ICON_ATLAS_SIZE;
            atlas.height = ICON_ATLAS_SIZE;
            const context = atlas.getContext("2d");
            if (!context)
                throw new Error("Unable to create the AOWIS network icon atlas");

            context.clearRect(0, 0, ICON_ATLAS_SIZE, ICON_ATLAS_SIZE);
            for (const entry of this.iconImages) {
                if (!entry || !entry.image)
                    continue;
                const slot = Number(entry.slot);
                if (!Number.isInteger(slot) || slot < 0
                    || slot >= ICON_ATLAS_GRID_SIZE * ICON_ATLAS_GRID_SIZE) {
                    continue;
                }
                const column = slot % ICON_ATLAS_GRID_SIZE;
                const row = Math.floor(slot / ICON_ATLAS_GRID_SIZE);
                const x = column * ICON_ATLAS_CELL_SIZE + ICON_ATLAS_PADDING;
                const y = row * ICON_ATLAS_CELL_SIZE + ICON_ATLAS_PADDING;
                const size = ICON_ATLAS_CELL_SIZE - ICON_ATLAS_PADDING * 2;
                context.drawImage(entry.image, x, y, size, size);
            }

            const gl = this.gl;
            gl.bindTexture(gl.TEXTURE_2D, resources.iconTexture);
            gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
            gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
            gl.texImage2D(
                gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA,
                gl.UNSIGNED_BYTE, atlas);
            gl.generateMipmap(gl.TEXTURE_2D);
            gl.bindTexture(gl.TEXTURE_2D, null);
            resources.iconRevision = this.iconRevision;
        }

        configureCanvas(width, height, pixelRatio) {
            const cssWidth = Math.max(1, Number(width) || 1);
            const cssHeight = Math.max(1, Number(height) || 1);
            const ratio = Math.max(1, Number(pixelRatio) || 1);
            const rasterWidth = Math.max(1, Math.ceil(cssWidth * ratio));
            const rasterHeight = Math.max(1, Math.ceil(cssHeight * ratio));
            if (this.canvas.width !== rasterWidth)
                this.canvas.width = rasterWidth;
            if (this.canvas.height !== rasterHeight)
                this.canvas.height = rasterHeight;

            const widthText = cssWidth + "px";
            const heightText = cssHeight + "px";
            if (this.canvas.style.width !== widthText)
                this.canvas.style.width = widthText;
            if (this.canvas.style.height !== heightText)
                this.canvas.style.height = heightText;
            return {
                width: cssWidth,
                height: cssHeight,
                pixelRatio: ratio
            };
        }

        setTransformUniforms(program, view) {
            const gl = this.gl;
            gl.uniform2f(program.translate, view.translateX, view.translateY);
            gl.uniform1f(program.scale, view.scale);
            gl.uniform2f(program.viewport, view.width, view.height);
        }

        render(view) {
            if (this.destroyed || this.contextLost || !this.resources)
                return false;
            if (typeof this.gl.isContextLost === "function" && this.gl.isContextLost())
                return false;

            const configuredView = this.configureCanvas(
                view.width, view.height, view.pixelRatio);
            configuredView.translateX = Number(view.translateX) || 0;
            configuredView.translateY = Number(view.translateY) || 0;
            configuredView.scale = Math.max(0.000001, Number(view.scale) || 1);
            this.uploadGeometry();
            this.uploadColors();
            this.uploadIconAtlas();

            const gl = this.gl;
            const resources = this.resources;
            gl.viewport(0, 0, this.canvas.width, this.canvas.height);
            gl.disable(gl.DEPTH_TEST);
            gl.disable(gl.CULL_FACE);
            gl.enable(gl.BLEND);
            gl.blendFuncSeparate(
                gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA,
                gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
            gl.clearColor(0, 0, 0, 0);
            gl.clear(gl.COLOR_BUFFER_BIT);

            if (resources.segmentCount > 0) {
                const program = resources.segmentProgram;
                gl.useProgram(program.handle);
                gl.bindVertexArray(resources.segmentVertexArray);
                this.setTransformUniforms(program, configuredView);
                gl.uniform1f(
                    program.halfWidth,
                    Math.max(0.5, Number(view.linkThickness) / 2 || 0.5));
                gl.uniform1f(program.pixelRatio, configuredView.pixelRatio);
                gl.drawArraysInstanced(
                    gl.TRIANGLES, 0, 6, resources.segmentCount);
            }

            if (resources.junctionCount > 0) {
                const program = resources.junctionProgram;
                gl.useProgram(program.handle);
                gl.bindVertexArray(resources.junctionVertexArray);
                this.setTransformUniforms(program, configuredView);
                gl.uniform1f(
                    program.radius,
                    Math.max(1, Number(view.junctionDiameter) / 2 || 1));
                gl.uniform1f(program.pixelRatio, configuredView.pixelRatio);
                gl.drawArraysInstanced(
                    gl.TRIANGLES, 0, 6, resources.junctionCount);
            }

            if (resources.iconCount > 0) {
                const program = resources.iconProgram;
                gl.useProgram(program.handle);
                gl.bindVertexArray(resources.iconVertexArray);
                this.setTransformUniforms(program, configuredView);
                gl.uniform1f(
                    program.halfSize,
                    Math.max(1, Number(view.iconSize) / 2 || 1));
                gl.activeTexture(gl.TEXTURE0);
                gl.bindTexture(gl.TEXTURE_2D, resources.iconTexture);
                gl.uniform1i(program.atlas, 0);
                gl.drawArraysInstanced(
                    gl.TRIANGLES, 0, 6, resources.iconCount);
            }

            gl.bindTexture(gl.TEXTURE_2D, null);
            gl.bindVertexArray(null);
            this.canvas.style.display = "block";
            return true;
        }

        clear() {
            this.canvas.style.display = "none";
            if (!this.gl || this.contextLost || !this.resources)
                return;
            this.gl.clearColor(0, 0, 0, 0);
            this.gl.clear(this.gl.COLOR_BUFFER_BIT);
        }

        destroy() {
            if (this.destroyed)
                return;
            this.destroyed = true;
            this.canvas.removeEventListener(
                "webglcontextlost", this.contextLostHandler, false);
            this.canvas.removeEventListener(
                "webglcontextrestored", this.contextRestoredHandler, false);
            if (!this.contextLost)
                disposeResources(this.gl, this.resources);
            this.resources = null;
            this.canvas.style.display = "none";
        }
    }

    function create(canvas, callbacks) {
        try {
            return new NetworkWebGlRenderer(canvas, callbacks);
        } catch (error) {
            console.error("Unable to initialize the AOWIS WebGL2 network renderer:", error);
            return null;
        }
    }

    window.aowisBrowserNetworkWebGl = Object.freeze({
        create: create
    });
})();
