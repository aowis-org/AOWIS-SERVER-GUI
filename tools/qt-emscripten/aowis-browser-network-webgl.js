(function () {
    "use strict";

    const BATCH_NAMES = Object.freeze(["base", "flowDirection", "selectionOuter", "overlay"]);
    const SPRITE_ATLAS_CELL_SIZE = 256;
    const SPRITE_ATLAS_GRID_SIZE = 4;
    const SPRITE_ATLAS_PADDING = 16;
    const SPRITE_ATLAS_CONTENT_SIZE =
        SPRITE_ATLAS_CELL_SIZE - SPRITE_ATLAS_PADDING * 2;
    const SPRITE_ATLAS_SIZE = SPRITE_ATLAS_CELL_SIZE * SPRITE_ATLAS_GRID_SIZE;
    const SPRITE_ATLAS_INSET = SPRITE_ATLAS_PADDING / SPRITE_ATLAS_SIZE;
    const FLOAT_SIZE = Float32Array.BYTES_PER_ELEMENT;

    const SEGMENT_VERTEX_SHADER = [
        "#version 300 es",
        "precision highp float;",
        "in vec2 a_start;",
        "in vec2 a_end;",
        "in vec4 a_color;",
        "uniform vec2 u_translate;",
        "uniform float u_scale;",
        "uniform vec2 u_viewport;",
        "uniform float u_half_width;",
        "out vec2 v_screen;",
        "flat out vec2 v_start;",
        "flat out vec2 v_end;",
        "flat out vec4 v_color;",
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
        "flat in vec4 v_color;",
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
        "    float coverage = 1.0 - smoothstep(",
        "        u_half_width - edge, u_half_width + edge, distance_from_segment);",
        "    float alpha = v_color.a * coverage;",
        "    if (alpha <= 0.0)",
        "        discard;",
        "    out_color = vec4(v_color.rgb, alpha);",
        "}"
    ].join("\n");

    const ARROW_VERTEX_SHADER = [
        "#version 300 es",
        "precision highp float;",
        "in vec2 a_center;",
        "in vec2 a_direction;",
        "in vec4 a_color;",
        "uniform vec2 u_translate;",
        "uniform float u_scale;",
        "uniform vec2 u_viewport;",
        "uniform float u_length;",
        "uniform float u_half_width;",
        "out vec2 v_screen;",
        "flat out vec2 v_start;",
        "flat out vec2 v_end;",
        "flat out vec4 v_color;",
        "const vec2 CORNERS[6] = vec2[6](",
        "    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0),",
        "    vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0, 1.0));",
        "void main() {",
        "    vec2 center_screen = u_translate + a_center * u_scale;",
        "    float input_length = length(a_direction);",
        "    vec2 direction = input_length > 0.0001",
        "        ? a_direction / input_length : vec2(1.0, 0.0);",
        "    vec2 normal = vec2(-direction.y, direction.x);",
        "    float arm_sign = gl_VertexID < 6 ? 1.0 : -1.0;",
        "    int corner_index = gl_VertexID < 6 ? gl_VertexID : gl_VertexID - 6;",
        "    vec2 tip = center_screen + direction * (u_length * 0.5);",
        "    vec2 tail = center_screen - direction * (u_length * 0.5)",
        "        + normal * (u_length * 0.4 * arm_sign);",
        "    vec2 arm = tip - tail;",
        "    float arm_length = length(arm);",
        "    vec2 arm_direction = arm_length > 0.0001 ? arm / arm_length : direction;",
        "    vec2 arm_normal = vec2(-arm_direction.y, arm_direction.x);",
        "    vec2 corner = CORNERS[corner_index];",
        "    vec2 base = corner.x < 0.0 ? tail : tip;",
        "    vec2 screen = base + arm_direction * corner.x * u_half_width",
        "        + arm_normal * corner.y * u_half_width;",
        "    vec2 clip = vec2(screen.x / u_viewport.x * 2.0 - 1.0,",
        "        1.0 - screen.y / u_viewport.y * 2.0);",
        "    gl_Position = vec4(clip, 0.0, 1.0);",
        "    v_screen = screen;",
        "    v_start = tail;",
        "    v_end = tip;",
        "    v_color = a_color;",
        "}"
    ].join("\n");

    const ARROW_FRAGMENT_SHADER = [
        "#version 300 es",
        "precision highp float;",
        "in vec2 v_screen;",
        "flat in vec2 v_start;",
        "flat in vec2 v_end;",
        "flat in vec4 v_color;",
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
        "    float coverage = 1.0 - smoothstep(",
        "        u_half_width - edge, u_half_width + edge, distance_from_segment);",
        "    float alpha = v_color.a * coverage;",
        "    if (alpha <= 0.0)",
        "        discard;",
        "    out_color = vec4(v_color.rgb, alpha);",
        "}"
    ].join("\n");

    const DISC_VERTEX_SHADER = [
        "#version 300 es",
        "precision highp float;",
        "in vec2 a_center;",
        "in float a_radius;",
        "in vec4 a_color;",
        "uniform vec2 u_translate;",
        "uniform float u_scale;",
        "uniform vec2 u_viewport;",
        "uniform float u_radius_scale;",
        "out vec2 v_corner;",
        "flat out vec4 v_color;",
        "flat out float v_radius;",
        "const vec2 CORNERS[6] = vec2[6](",
        "    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0),",
        "    vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0, 1.0));",
        "void main() {",
        "    vec2 corner = CORNERS[gl_VertexID];",
        "    float radius = max(0.0, a_radius * u_radius_scale);",
        "    vec2 screen = u_translate + a_center * u_scale + corner * radius;",
        "    vec2 clip = vec2(screen.x / u_viewport.x * 2.0 - 1.0,",
        "        1.0 - screen.y / u_viewport.y * 2.0);",
        "    gl_Position = vec4(clip, 0.0, 1.0);",
        "    v_corner = corner;",
        "    v_color = a_color;",
        "    v_radius = radius;",
        "}"
    ].join("\n");

    const DISC_FRAGMENT_SHADER = [
        "#version 300 es",
        "precision highp float;",
        "in vec2 v_corner;",
        "flat in vec4 v_color;",
        "flat in float v_radius;",
        "uniform float u_pixel_ratio;",
        "out vec4 out_color;",
        "void main() {",
        "    float distance_from_center = length(v_corner);",
        "    float edge = max(fwidth(distance_from_center),",
        "        0.5 / max(1.0, v_radius * u_pixel_ratio));",
        "    float coverage = 1.0 - smoothstep(",
        "        1.0 - edge, 1.0 + edge, distance_from_center);",
        "    float alpha = v_color.a * coverage;",
        "    if (alpha <= 0.0)",
        "        discard;",
        "    out_color = vec4(v_color.rgb, alpha);",
        "}"
    ].join("\n");

    const SPRITE_VERTEX_SHADER = [
        "#version 300 es",
        "precision highp float;",
        "in vec2 a_center;",
        "in vec2 a_size;",
        "in vec2 a_anchor;",
        "in vec2 a_offset;",
        "in float a_slot;",
        "in float a_tint;",
        "in vec4 a_color;",
        "uniform vec2 u_translate;",
        "uniform float u_scale;",
        "uniform vec2 u_viewport;",
        "uniform float u_size_scale;",
        "out vec2 v_uv;",
        "flat out vec4 v_color;",
        "flat out float v_tint;",
        "const float GRID_SIZE = " + SPRITE_ATLAS_GRID_SIZE.toFixed(1) + ";",
        "const float ATLAS_INSET = " + SPRITE_ATLAS_INSET.toFixed(8) + ";",
        "const vec2 CORNERS[6] = vec2[6](",
        "    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),",
        "    vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0));",
        "void main() {",
        "    vec2 corner = CORNERS[gl_VertexID];",
        "    vec2 size = a_size * u_size_scale;",
        "    vec2 screen = u_translate + a_center * u_scale + a_offset",
        "        + (corner - a_anchor) * size;",
        "    vec2 clip = vec2(screen.x / u_viewport.x * 2.0 - 1.0,",
        "        1.0 - screen.y / u_viewport.y * 2.0);",
        "    gl_Position = vec4(clip, 0.0, 1.0);",
        "    float column = mod(a_slot, GRID_SIZE);",
        "    float row = floor(a_slot / GRID_SIZE);",
        "    vec2 cell_origin = vec2(column, row) / GRID_SIZE;",
        "    vec2 uv_minimum = cell_origin + vec2(ATLAS_INSET);",
        "    vec2 uv_maximum = cell_origin + vec2(1.0 / GRID_SIZE - ATLAS_INSET);",
        "    v_uv = mix(uv_minimum, uv_maximum, corner);",
        "    v_color = a_color;",
        "    v_tint = a_tint;",
        "}"
    ].join("\n");

    const SPRITE_FRAGMENT_SHADER = [
        "#version 300 es",
        "precision highp float;",
        "in vec2 v_uv;",
        "flat in vec4 v_color;",
        "flat in float v_tint;",
        "uniform sampler2D u_atlas;",
        "out vec4 out_color;",
        "void main() {",
        "    vec4 sampled = texture(u_atlas, v_uv);",
        "    float alpha = sampled.a * v_color.a;",
        "    if (alpha <= 0.001)",
        "        discard;",
        "    vec3 color = sampled.rgb;",
        "    if (v_tint >= 1.5) {",
        "        float maximum_channel = max(sampled.r, max(sampled.g, sampled.b));",
        "        float minimum_channel = min(sampled.r, min(sampled.g, sampled.b));",
        "        float fill_mask = smoothstep(0.08, 0.20,",
        "            maximum_channel - minimum_channel);",
        "        color = mix(sampled.rgb, v_color.rgb, fill_mask);",
        "    } else if (v_tint >= 0.5) {",
        "        color = v_color.rgb;",
        "    }",
        "    out_color = vec4(color, alpha);",
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
            throw new Error("Unable to create the AOWIS network sprite texture");
        return texture;
    }

    function configureInstanceAttribute(gl, location, size, stride, offset) {
        gl.enableVertexAttribArray(location);
        gl.vertexAttribPointer(location, size, gl.FLOAT, false, stride, offset);
        gl.vertexAttribDivisor(location, 1);
    }

    function createBatchResources(gl, programs, resources) {
        const batch = {
            geometryRevision: 0,
            colorRevision: 0,
            segmentCount: 0,
            arrowCount: 0,
            discCount: 0,
            spriteCount: 0,
            segmentColorLength: -1,
            arrowColorLength: -1,
            discColorLength: -1,
            spriteColorLength: -1
        };

        batch.segmentGeometryBuffer = createBuffer(gl);
        batch.segmentColorBuffer = createBuffer(gl);
        batch.arrowGeometryBuffer = createBuffer(gl);
        batch.arrowColorBuffer = createBuffer(gl);
        batch.discGeometryBuffer = createBuffer(gl);
        batch.discColorBuffer = createBuffer(gl);
        batch.spriteGeometryBuffer = createBuffer(gl);
        batch.spriteColorBuffer = createBuffer(gl);
        resources.buffers.push(
            batch.segmentGeometryBuffer,
            batch.segmentColorBuffer,
            batch.arrowGeometryBuffer,
            batch.arrowColorBuffer,
            batch.discGeometryBuffer,
            batch.discColorBuffer,
            batch.spriteGeometryBuffer,
            batch.spriteColorBuffer);

        batch.segmentVertexArray = createVertexArray(gl);
        batch.arrowVertexArray = createVertexArray(gl);
        batch.discVertexArray = createVertexArray(gl);
        batch.spriteVertexArray = createVertexArray(gl);
        resources.vertexArrays.push(
            batch.segmentVertexArray,
            batch.arrowVertexArray,
            batch.discVertexArray,
            batch.spriteVertexArray);

        gl.bindVertexArray(batch.segmentVertexArray);
        gl.bindBuffer(gl.ARRAY_BUFFER, batch.segmentGeometryBuffer);
        configureInstanceAttribute(gl, programs.segment.start, 2, 4 * FLOAT_SIZE, 0);
        configureInstanceAttribute(
            gl, programs.segment.end, 2, 4 * FLOAT_SIZE, 2 * FLOAT_SIZE);
        gl.bindBuffer(gl.ARRAY_BUFFER, batch.segmentColorBuffer);
        configureInstanceAttribute(gl, programs.segment.color, 4, 4 * FLOAT_SIZE, 0);

        gl.bindVertexArray(batch.arrowVertexArray);
        gl.bindBuffer(gl.ARRAY_BUFFER, batch.arrowGeometryBuffer);
        configureInstanceAttribute(gl, programs.arrow.center, 2, 4 * FLOAT_SIZE, 0);
        configureInstanceAttribute(
            gl, programs.arrow.direction, 2, 4 * FLOAT_SIZE, 2 * FLOAT_SIZE);
        gl.bindBuffer(gl.ARRAY_BUFFER, batch.arrowColorBuffer);
        configureInstanceAttribute(gl, programs.arrow.color, 4, 4 * FLOAT_SIZE, 0);

        gl.bindVertexArray(batch.discVertexArray);
        gl.bindBuffer(gl.ARRAY_BUFFER, batch.discGeometryBuffer);
        configureInstanceAttribute(gl, programs.disc.center, 2, 3 * FLOAT_SIZE, 0);
        configureInstanceAttribute(
            gl, programs.disc.radius, 1, 3 * FLOAT_SIZE, 2 * FLOAT_SIZE);
        gl.bindBuffer(gl.ARRAY_BUFFER, batch.discColorBuffer);
        configureInstanceAttribute(gl, programs.disc.color, 4, 4 * FLOAT_SIZE, 0);

        gl.bindVertexArray(batch.spriteVertexArray);
        gl.bindBuffer(gl.ARRAY_BUFFER, batch.spriteGeometryBuffer);
        configureInstanceAttribute(gl, programs.sprite.center, 2, 10 * FLOAT_SIZE, 0);
        configureInstanceAttribute(
            gl, programs.sprite.size, 2, 10 * FLOAT_SIZE, 2 * FLOAT_SIZE);
        configureInstanceAttribute(
            gl, programs.sprite.anchor, 2, 10 * FLOAT_SIZE, 4 * FLOAT_SIZE);
        configureInstanceAttribute(
            gl, programs.sprite.offset, 2, 10 * FLOAT_SIZE, 6 * FLOAT_SIZE);
        configureInstanceAttribute(
            gl, programs.sprite.slot, 1, 10 * FLOAT_SIZE, 8 * FLOAT_SIZE);
        configureInstanceAttribute(
            gl, programs.sprite.tint, 1, 10 * FLOAT_SIZE, 9 * FLOAT_SIZE);
        gl.bindBuffer(gl.ARRAY_BUFFER, batch.spriteColorBuffer);
        configureInstanceAttribute(gl, programs.sprite.color, 4, 4 * FLOAT_SIZE, 0);
        return batch;
    }

    function disposeResources(gl, resources) {
        if (!gl || !resources)
            return;

        for (const vertexArray of resources.vertexArrays)
            gl.deleteVertexArray(vertexArray);
        for (const buffer of resources.buffers)
            gl.deleteBuffer(buffer);
        if (resources.spriteTexture)
            gl.deleteTexture(resources.spriteTexture);
        for (const program of resources.programHandles)
            gl.deleteProgram(program);
    }

    function createResources(gl) {
        const resources = {
            programHandles: [],
            buffers: [],
            vertexArrays: [],
            batches: Object.create(null),
            spriteTexture: null,
            spriteRevision: 0
        };

        try {
            const segmentHandle = createProgram(
                gl, SEGMENT_VERTEX_SHADER, SEGMENT_FRAGMENT_SHADER, "network segment");
            const arrowHandle = createProgram(
                gl, ARROW_VERTEX_SHADER, ARROW_FRAGMENT_SHADER, "network flow arrow");
            const discHandle = createProgram(
                gl, DISC_VERTEX_SHADER, DISC_FRAGMENT_SHADER, "network disc");
            const spriteHandle = createProgram(
                gl, SPRITE_VERTEX_SHADER, SPRITE_FRAGMENT_SHADER, "network sprite");
            resources.programHandles.push(segmentHandle, arrowHandle, discHandle, spriteHandle);

            resources.programs = {
                segment: {
                    handle: segmentHandle,
                    start: attributeLocation(gl, segmentHandle, "a_start"),
                    end: attributeLocation(gl, segmentHandle, "a_end"),
                    color: attributeLocation(gl, segmentHandle, "a_color"),
                    translate: uniformLocation(gl, segmentHandle, "u_translate"),
                    scale: uniformLocation(gl, segmentHandle, "u_scale"),
                    viewport: uniformLocation(gl, segmentHandle, "u_viewport"),
                    halfWidth: uniformLocation(gl, segmentHandle, "u_half_width"),
                    pixelRatio: uniformLocation(gl, segmentHandle, "u_pixel_ratio")
                },
                arrow: {
                    handle: arrowHandle,
                    center: attributeLocation(gl, arrowHandle, "a_center"),
                    direction: attributeLocation(gl, arrowHandle, "a_direction"),
                    color: attributeLocation(gl, arrowHandle, "a_color"),
                    translate: uniformLocation(gl, arrowHandle, "u_translate"),
                    scale: uniformLocation(gl, arrowHandle, "u_scale"),
                    viewport: uniformLocation(gl, arrowHandle, "u_viewport"),
                    length: uniformLocation(gl, arrowHandle, "u_length"),
                    halfWidth: uniformLocation(gl, arrowHandle, "u_half_width"),
                    pixelRatio: uniformLocation(gl, arrowHandle, "u_pixel_ratio")
                },
                disc: {
                    handle: discHandle,
                    center: attributeLocation(gl, discHandle, "a_center"),
                    radius: attributeLocation(gl, discHandle, "a_radius"),
                    color: attributeLocation(gl, discHandle, "a_color"),
                    translate: uniformLocation(gl, discHandle, "u_translate"),
                    scale: uniformLocation(gl, discHandle, "u_scale"),
                    viewport: uniformLocation(gl, discHandle, "u_viewport"),
                    radiusScale: uniformLocation(gl, discHandle, "u_radius_scale"),
                    pixelRatio: uniformLocation(gl, discHandle, "u_pixel_ratio")
                },
                sprite: {
                    handle: spriteHandle,
                    center: attributeLocation(gl, spriteHandle, "a_center"),
                    size: attributeLocation(gl, spriteHandle, "a_size"),
                    anchor: attributeLocation(gl, spriteHandle, "a_anchor"),
                    offset: attributeLocation(gl, spriteHandle, "a_offset"),
                    slot: attributeLocation(gl, spriteHandle, "a_slot"),
                    tint: attributeLocation(gl, spriteHandle, "a_tint"),
                    color: attributeLocation(gl, spriteHandle, "a_color"),
                    translate: uniformLocation(gl, spriteHandle, "u_translate"),
                    scale: uniformLocation(gl, spriteHandle, "u_scale"),
                    viewport: uniformLocation(gl, spriteHandle, "u_viewport"),
                    sizeScale: uniformLocation(gl, spriteHandle, "u_size_scale"),
                    atlas: uniformLocation(gl, spriteHandle, "u_atlas")
                }
            };

            for (const batchName of BATCH_NAMES) {
                resources.batches[batchName] = createBatchResources(
                    gl, resources.programs, resources);
            }

            resources.spriteTexture = createTexture(gl);
            gl.bindTexture(gl.TEXTURE_2D, resources.spriteTexture);
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

    function emptyBatch() {
        return {
            segmentGeometry: new Float32Array(0),
            arrowGeometry: new Float32Array(0),
            discGeometry: new Float32Array(0),
            spriteGeometry: new Float32Array(0),
            segmentColors: new Float32Array(0),
            arrowColors: new Float32Array(0),
            discColors: new Float32Array(0),
            spriteColors: new Float32Array(0),
            geometryRevision: 1,
            colorRevision: 1
        };
    }

    function closeSpriteImages(spriteImages) {
        for (const entry of spriteImages) {
            if (entry && entry.image && typeof entry.image.close === "function")
                entry.image.close();
        }
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
            if (typeof createImageBitmap !== "function")
                throw new Error("ImageBitmap resizing is unavailable");

            this.resources = createResources(this.gl);
            this.batches = Object.create(null);
            for (const batchName of BATCH_NAMES)
                this.batches[batchName] = emptyBatch();
            this.spriteImages = [];
            this.spriteImageGeneration = 0;
            this.spriteRevision = 1;
            this.contextLost = false;
            this.destroyed = false;
            this.contextLostHandler = (event) => this.handleContextLost(event);
            this.contextRestoredHandler = () => this.handleContextRestored();
            canvas.addEventListener("webglcontextlost", this.contextLostHandler, false);
            canvas.addEventListener("webglcontextrestored", this.contextRestoredHandler, false);
        }

        batch(batchName) {
            const batch = this.batches[batchName];
            if (!batch)
                throw new TypeError("Unknown AOWIS WebGL network batch: " + batchName);
            return batch;
        }

        setGeometry(batchName, geometry) {
            const batch = this.batch(batchName);
            const source = geometry || {};
            batch.segmentGeometry = floatData(
                source.segments, 4, batchName + " segment geometry");
            batch.arrowGeometry = floatData(
                source.arrows, 4, batchName + " arrow geometry");
            batch.discGeometry = floatData(
                source.discs, 3, batchName + " disc geometry");
            batch.spriteGeometry = floatData(
                source.sprites, 10, batchName + " sprite geometry");
            ++batch.geometryRevision;
        }

        setColors(batchName, colors) {
            const batch = this.batch(batchName);
            const source = colors || {};
            batch.segmentColors = floatData(
                source.segments, 4, batchName + " segment color");
            batch.arrowColors = floatData(
                source.arrows, 4, batchName + " arrow color");
            batch.discColors = floatData(
                source.discs, 4, batchName + " disc color");
            batch.spriteColors = floatData(
                source.sprites, 4, batchName + " sprite color");
            ++batch.colorRevision;
        }

        clearBatch(batchName) {
            this.setGeometry(batchName, null);
            this.setColors(batchName, null);
        }

        setSpriteImages(spriteImages) {
            const sources = Array.isArray(spriteImages)
                ? spriteImages.filter((entry) => entry && entry.image) : [];
            const generation = ++this.spriteImageGeneration;
            if (sources.length === 0) {
                closeSpriteImages(this.spriteImages);
                this.spriteImages = [];
                ++this.spriteRevision;
                return;
            }

            Promise.all(sources.map(async (entry) => {
                try {
                    const image = await createImageBitmap(entry.image, {
                        resizeWidth: SPRITE_ATLAS_CONTENT_SIZE,
                        resizeHeight: SPRITE_ATLAS_CONTENT_SIZE,
                        resizeQuality: "high"
                    });
                    return { slot: Number(entry.slot), image: image };
                } catch (error) {
                    console.error("Unable to prepare an AOWIS WebGL network sprite:", error);
                    return null;
                }
            })).then((entries) => {
                const prepared = entries.filter((entry) => entry !== null);
                if (this.destroyed || generation !== this.spriteImageGeneration) {
                    closeSpriteImages(prepared);
                    return;
                }
                closeSpriteImages(this.spriteImages);
                this.spriteImages = prepared;
                ++this.spriteRevision;
                if (typeof this.callbacks.spritesReady === "function")
                    this.callbacks.spritesReady();
            });
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

        uploadGeometry(batchName) {
            const batch = this.batches[batchName];
            const resources = this.resources.batches[batchName];
            if (resources.geometryRevision === batch.geometryRevision)
                return;

            uploadStaticBuffer(
                this.gl, resources.segmentGeometryBuffer, batch.segmentGeometry);
            uploadStaticBuffer(
                this.gl, resources.arrowGeometryBuffer, batch.arrowGeometry);
            uploadStaticBuffer(
                this.gl, resources.discGeometryBuffer, batch.discGeometry);
            uploadStaticBuffer(
                this.gl, resources.spriteGeometryBuffer, batch.spriteGeometry);
            resources.segmentCount = batch.segmentGeometry.length / 4;
            resources.arrowCount = batch.arrowGeometry.length / 4;
            resources.discCount = batch.discGeometry.length / 3;
            resources.spriteCount = batch.spriteGeometry.length / 10;
            resources.geometryRevision = batch.geometryRevision;
            resources.colorRevision = 0;
        }

        uploadColors(batchName) {
            const batch = this.batches[batchName];
            const resources = this.resources.batches[batchName];
            if (resources.colorRevision === batch.colorRevision)
                return;
            if (batch.segmentColors.length !== resources.segmentCount * 4
                || batch.arrowColors.length !== resources.arrowCount * 4
                || batch.discColors.length !== resources.discCount * 4
                || batch.spriteColors.length !== resources.spriteCount * 4) {
                throw new Error(
                    "AOWIS network WebGL " + batchName + " geometry and color counts differ");
            }

            resources.segmentColorLength = uploadColorBuffer(
                this.gl,
                resources.segmentColorBuffer,
                batch.segmentColors,
                resources.segmentColorLength);
            resources.arrowColorLength = uploadColorBuffer(
                this.gl,
                resources.arrowColorBuffer,
                batch.arrowColors,
                resources.arrowColorLength);
            resources.discColorLength = uploadColorBuffer(
                this.gl,
                resources.discColorBuffer,
                batch.discColors,
                resources.discColorLength);
            resources.spriteColorLength = uploadColorBuffer(
                this.gl,
                resources.spriteColorBuffer,
                batch.spriteColors,
                resources.spriteColorLength);
            resources.colorRevision = batch.colorRevision;
        }

        uploadSpriteAtlas() {
            const resources = this.resources;
            if (resources.spriteRevision === this.spriteRevision)
                return;

            const gl = this.gl;
            gl.bindTexture(gl.TEXTURE_2D, resources.spriteTexture);
            gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
            gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
            gl.texImage2D(
                gl.TEXTURE_2D, 0, gl.RGBA,
                SPRITE_ATLAS_SIZE, SPRITE_ATLAS_SIZE, 0,
                gl.RGBA, gl.UNSIGNED_BYTE, null);
            for (const entry of this.spriteImages) {
                if (!entry || !entry.image)
                    continue;
                const slot = Number(entry.slot);
                if (!Number.isInteger(slot) || slot < 0
                    || slot >= SPRITE_ATLAS_GRID_SIZE * SPRITE_ATLAS_GRID_SIZE) {
                    continue;
                }
                const column = slot % SPRITE_ATLAS_GRID_SIZE;
                const row = Math.floor(slot / SPRITE_ATLAS_GRID_SIZE);
                const x = column * SPRITE_ATLAS_CELL_SIZE + SPRITE_ATLAS_PADDING;
                const y = row * SPRITE_ATLAS_CELL_SIZE + SPRITE_ATLAS_PADDING;
                gl.texSubImage2D(
                    gl.TEXTURE_2D, 0, x, y,
                    gl.RGBA, gl.UNSIGNED_BYTE, entry.image);
            }
            gl.generateMipmap(gl.TEXTURE_2D);
            gl.bindTexture(gl.TEXTURE_2D, null);
            resources.spriteRevision = this.spriteRevision;
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

        renderBatch(batchName, view, style, drawNonSprites, drawSprites) {
            const gl = this.gl;
            const resources = this.resources;
            const batch = resources.batches[batchName];
            const batchStyle = style || {};
            const renderNonSprites = drawNonSprites === undefined ? true : !!drawNonSprites;
            const renderSprites = drawSprites === undefined ? true : !!drawSprites;

            if (renderNonSprites && batch.segmentCount > 0) {
                const program = resources.programs.segment;
                gl.useProgram(program.handle);
                gl.bindVertexArray(batch.segmentVertexArray);
                this.setTransformUniforms(program, view);
                gl.uniform1f(
                    program.halfWidth,
                    Math.max(0.5, (Number(batchStyle.segmentWidth) || 1) / 2));
                gl.uniform1f(program.pixelRatio, view.pixelRatio);
                gl.drawArraysInstanced(gl.TRIANGLES, 0, 6, batch.segmentCount);
            }

            if (renderNonSprites && batch.arrowCount > 0) {
                const program = resources.programs.arrow;
                gl.useProgram(program.handle);
                gl.bindVertexArray(batch.arrowVertexArray);
                this.setTransformUniforms(program, view);
                gl.uniform1f(
                    program.length, Math.max(4, Number(batchStyle.arrowLength) || 10));
                gl.uniform1f(
                    program.halfWidth,
                    Math.max(0.5, (Number(batchStyle.arrowWidth) || 2) / 2));
                gl.uniform1f(program.pixelRatio, view.pixelRatio);
                gl.drawArraysInstanced(gl.TRIANGLES, 0, 12, batch.arrowCount);
            }

            if (renderNonSprites && batch.discCount > 0) {
                const program = resources.programs.disc;
                gl.useProgram(program.handle);
                gl.bindVertexArray(batch.discVertexArray);
                this.setTransformUniforms(program, view);
                gl.uniform1f(
                    program.radiusScale,
                    Math.max(0.000001, Number(batchStyle.discScale) || 1));
                gl.uniform1f(program.pixelRatio, view.pixelRatio);
                gl.drawArraysInstanced(gl.TRIANGLES, 0, 6, batch.discCount);
            }

            if (renderSprites && batch.spriteCount > 0) {
                const program = resources.programs.sprite;
                gl.useProgram(program.handle);
                gl.bindVertexArray(batch.spriteVertexArray);
                this.setTransformUniforms(program, view);
                gl.uniform1f(
                    program.sizeScale,
                    Math.max(0.000001, Number(batchStyle.spriteScale) || 1));
                gl.activeTexture(gl.TEXTURE0);
                gl.bindTexture(gl.TEXTURE_2D, resources.spriteTexture);
                gl.uniform1i(program.atlas, 0);
                gl.drawArraysInstanced(gl.TRIANGLES, 0, 6, batch.spriteCount);
            }
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
            for (const batchName of BATCH_NAMES) {
                this.uploadGeometry(batchName);
                this.uploadColors(batchName);
            }
            this.uploadSpriteAtlas();

            const gl = this.gl;
            gl.viewport(0, 0, this.canvas.width, this.canvas.height);
            gl.disable(gl.DEPTH_TEST);
            gl.disable(gl.CULL_FACE);
            gl.enable(gl.BLEND);
            gl.blendFuncSeparate(
                gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA,
                gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
            gl.clearColor(0, 0, 0, 0);
            gl.clear(gl.COLOR_BUFFER_BIT);

            const styles = view.batches || {};
            this.renderBatch("base", configuredView, styles.base, true, false);
            this.renderBatch(
                "flowDirection", configuredView, styles.flowDirection, true, false);
            this.renderBatch(
                "selectionOuter", configuredView, styles.selectionOuter, true, false);
            this.renderBatch("overlay", configuredView, styles.overlay, true, false);
            // Device/node icons are semantic markers, not link geometry. Draw
            // every sprite pass after all network strokes so links, arrows and
            // highlight strokes cannot cut through pumps, valves, reservoirs
            // or tanks. Selection/diagnostic sprite passes still remain above
            // the base icon itself.
            this.renderBatch("base", configuredView, styles.base, false, true);
            this.renderBatch(
                "selectionOuter", configuredView, styles.selectionOuter, false, true);
            this.renderBatch("overlay", configuredView, styles.overlay, false, true);

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
            ++this.spriteImageGeneration;
            closeSpriteImages(this.spriteImages);
            this.spriteImages = [];
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
