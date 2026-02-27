#include "page_curl_renderer.h"
#include <android/log.h>
#include <cmath>
#include <cstring>

#define LOG_TAG "PageCurlRenderer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ---------------------------------------------------------------------------
// GLSL shaders
// ---------------------------------------------------------------------------

// Curl shader: applies cylindrical transform to vertices right of foldX.
// In BACK_FACE mode the UV is mirrored horizontally across the fold line,
// showing the reverse side of the page content.
static const char* kCurlVert = R"GLSL(#version 300 es
precision highp float;

in  vec2 aPos;
in  vec2 aUV;

uniform float uFoldX;     // fold line x [0,1]
uniform float uRadius;    // cylinder radius [0,1]
uniform bool  uBackFace;  // true => rendering back face (mirrored)
uniform float uDarken;    // max darken amount for curl shading

out vec2  vUV;
out float vShadow;

const float PI = 3.14159265;

void main() {
    vec2  pos = aPos;
    vec2  uv  = aUV;
    float z   = 0.0;

    float dx = pos.x - uFoldX;
    if (dx > 0.0) {
        float theta = min(dx / uRadius, PI);
        pos.x = uFoldX + uRadius * sin(theta);
        z     = uRadius * (1.0 - cos(theta));

        if (uBackFace) {
            // Mirror UV so the back shows the reverse of the same page
            uv.x = 2.0 * uFoldX - aUV.x;
            // Clamp so we don't sample outside texture
            uv.x = clamp(uv.x, 0.0, 1.0);
        }

        // Shadow intensity: maximum at theta = PI/2 (edge of cylinder)
        vShadow = uDarken * sin(theta);
    } else {
        vShadow = 0.0;
    }

    // [0,1] → NDC; Y is flipped because bitmap row 0 = top
    gl_Position = vec4(pos.x * 2.0 - 1.0,
                       1.0 - pos.y * 2.0,
                       -z * 0.5,          // negative z = further back (back face behind)
                       1.0);
    vUV = uv;
}
)GLSL";

static const char* kCurlFrag = R"GLSL(#version 300 es
precision mediump float;

in  vec2  vUV;
in  float vShadow;

uniform sampler2D uTex;

out vec4 fragColor;

void main() {
    vec4 color = texture(uTex, vUV);
    color.rgb *= (1.0 - vShadow * 0.45);
    fragColor   = color;
}
)GLSL";

// Flat shader: draws a full-page textured quad with no transforms.
static const char* kFlatVert = R"GLSL(#version 300 es
precision highp float;

in  vec2 aPos;
in  vec2 aUV;

out vec2 vUV;

void main() {
    gl_Position = vec4(aPos.x * 2.0 - 1.0,
                       1.0 - aPos.y * 2.0,
                       0.0, 1.0);
    vUV = aUV;
}
)GLSL";

static const char* kFlatFrag = R"GLSL(#version 300 es
precision mediump float;

in  vec2 vUV;

uniform sampler2D uFlatTex;

out vec4 fragColor;

void main() {
    fragColor = texture(uFlatTex, vUV);
}
)GLSL";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

GLuint PageCurlRenderer::compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[1024];
        glGetShaderInfoLog(shader, sizeof(buf), nullptr, buf);
        LOGE("Shader compile error: %s", buf);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint PageCurlRenderer::createProgram(const char* vertSrc, const char* fragSrc) {
    GLuint vs = compileShader(GL_VERTEX_SHADER,   vertSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragSrc);
    if (!vs || !fs) return 0;

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "aPos");
    glBindAttribLocation(prog, 1, "aUV");
    glLinkProgram(prog);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[1024];
        glGetProgramInfoLog(prog, sizeof(buf), nullptr, buf);
        LOGE("Program link error: %s", buf);
        glDeleteProgram(prog);
        prog = 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

// ---------------------------------------------------------------------------
// Mesh: (MESH_COLS+1)*(MESH_ROWS+1) vertices in [0,1]x[0,1], indexed quads.
// ---------------------------------------------------------------------------

void PageCurlRenderer::buildMesh() {
    const int cols = MESH_COLS;
    const int rows = MESH_ROWS;
    const int vCount = (cols + 1) * (rows + 1);

    struct Vertex { float x, y, u, v; };
    std::vector<Vertex> verts(vCount);

    int idx = 0;
    for (int r = 0; r <= rows; ++r) {
        for (int c = 0; c <= cols; ++c) {
            float u = static_cast<float>(c) / cols;
            float v = static_cast<float>(r) / rows;
            verts[idx++] = { u, v, u, v };
        }
    }

    std::vector<uint16_t> indices;
    indices.reserve(cols * rows * 6);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            uint16_t tl = static_cast<uint16_t>( r      * (cols + 1) + c    );
            uint16_t tr = static_cast<uint16_t>( r      * (cols + 1) + c + 1);
            uint16_t bl = static_cast<uint16_t>((r + 1) * (cols + 1) + c    );
            uint16_t br = static_cast<uint16_t>((r + 1) * (cols + 1) + c + 1);
            indices.push_back(tl); indices.push_back(bl); indices.push_back(tr);
            indices.push_back(tr); indices.push_back(bl); indices.push_back(br);
        }
    }
    mIndexCount = static_cast<int>(indices.size());

    glGenBuffers(1, &mVBO);
    glBindBuffer(GL_ARRAY_BUFFER, mVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)),
                 verts.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &mEBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(uint16_t)),
                 indices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

PageCurlRenderer::PageCurlRenderer() = default;

PageCurlRenderer::~PageCurlRenderer() {
    release();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void PageCurlRenderer::onSurfaceCreated() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    mProgram = createProgram(kCurlVert, kCurlFrag);
    if (mProgram) {
        mUFoldX    = glGetUniformLocation(mProgram, "uFoldX");
        mURadius   = glGetUniformLocation(mProgram, "uRadius");
        mUBackFace = glGetUniformLocation(mProgram, "uBackFace");
        mUDarken   = glGetUniformLocation(mProgram, "uDarken");
        mUTex      = glGetUniformLocation(mProgram, "uTex");
    }

    mFlatProgram = createProgram(kFlatVert, kFlatFrag);
    if (mFlatProgram) {
        mUFlatTex = glGetUniformLocation(mFlatProgram, "uFlatTex");
    }

    glGenTextures(3, mTextures);
    for (int i = 0; i < 3; ++i) {
        glBindTexture(GL_TEXTURE_2D, mTextures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    buildMesh();
    LOGI("onSurfaceCreated done");
}

void PageCurlRenderer::onSurfaceChanged(int width, int height) {
    mSurfaceW = width;
    mSurfaceH = height;
    glViewport(0, 0, width, height);
    LOGI("onSurfaceChanged %dx%d", width, height);
}

void PageCurlRenderer::setTexture(int slot, const uint8_t* rgba, int texWidth, int texHeight) {
    if (slot < 0 || slot >= 3) return;
    glBindTexture(GL_TEXTURE_2D, mTextures[slot]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 texWidth, texHeight, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void PageCurlRenderer::release() {
    if (mVBO)          { glDeleteBuffers(1, &mVBO);          mVBO = 0; }
    if (mEBO)          { glDeleteBuffers(1, &mEBO);          mEBO = 0; }
    if (mProgram)      { glDeleteProgram(mProgram);          mProgram = 0; }
    if (mFlatProgram)  { glDeleteProgram(mFlatProgram);      mFlatProgram = 0; }
    for (int i = 0; i < 3; ++i) {
        if (mTextures[i]) { glDeleteTextures(1, &mTextures[i]); mTextures[i] = 0; }
    }
}

// ---------------------------------------------------------------------------
// Internal draw helpers
// ---------------------------------------------------------------------------

static void bindMeshAttribs(GLuint vbo, GLuint ebo) {
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    // aPos at location 0 (xy), aUV at location 1 (zw)
    const GLsizei stride = 4 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(2 * sizeof(float)));
}

void PageCurlRenderer::drawFlat(int texSlot) {
    if (!mFlatProgram || !mTextures[texSlot]) return;
    glUseProgram(mFlatProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mTextures[texSlot]);
    glUniform1i(mUFlatTex, 0);
    bindMeshAttribs(mVBO, mEBO);
    glDrawElements(GL_TRIANGLES, mIndexCount, GL_UNSIGNED_SHORT, nullptr);
}

void PageCurlRenderer::drawCurl(int texSlot, float foldX, bool backFace, float darken) {
    if (!mProgram || !mTextures[texSlot]) return;
    glUseProgram(mProgram);

    if (backFace) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);  // show back face
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);   // show front face
    }

    glUniform1f(mUFoldX,    foldX);
    glUniform1f(mURadius,   CURL_RADIUS);
    glUniform1i(mUBackFace, backFace ? 1 : 0);
    glUniform1f(mUDarken,   darken);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mTextures[texSlot]);
    glUniform1i(mUTex, 0);

    bindMeshAttribs(mVBO, mEBO);
    glDrawElements(GL_TRIANGLES, mIndexCount, GL_UNSIGNED_SHORT, nullptr);

    glDisable(GL_CULL_FACE);
}

// ---------------------------------------------------------------------------
// Public draw calls
// ---------------------------------------------------------------------------

// Forward curl: current page curls right-to-left, revealing next page.
// foldX travels from 1.0 (no curl) toward 0.0 (fully turned).
void PageCurlRenderer::drawForward(float foldX) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 1. Next page flat (underneath on the right of the fold line)
    if (mTextures[TEX_NEXT]) {
        drawFlat(TEX_NEXT);
    }

    // 2. Shadow cast on the revealed next page (gradient left of foldX rightwards)
    drawRevealShadow(foldX);

    // 3. Current page cylinder — back face (lighter, slightly darkened)
    //    This is the paper back showing as the page curls past 90°
    drawCurl(TEX_CURRENT, foldX, /*backFace=*/true, /*darken=*/0.15f);

    // 4. Current page cylinder — front face (the actual page content curling away)
    drawCurl(TEX_CURRENT, foldX, /*backFace=*/false, /*darken=*/0.35f);

    // 5. Shadow on the still-flat portion of current page (near fold line left side)
    drawFlatShadow(foldX);
}

// Backward curl: previous page slides in from left, covering current page.
// foldX travels from 0.0 (no curl) toward 1.0 (fully turned).
void PageCurlRenderer::drawBackward(float foldX) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 1. Current page flat (underneath)
    if (mTextures[TEX_CURRENT]) {
        drawFlat(TEX_CURRENT);
    }

    // 2. Shadow on the current page to the right of foldX
    drawRevealShadow(foldX);

    // 3. Previous page cylinder — back face
    //    For backward curl the fold is on the LEFT side, so we invert foldX
    float mirrorFoldX = 1.0f - foldX;
    // We reuse the same shader by rendering the prev page mirrored
    drawCurl(TEX_PREV, mirrorFoldX, /*backFace=*/true, /*darken=*/0.15f);

    // 4. Previous page cylinder — front face
    drawCurl(TEX_PREV, mirrorFoldX, /*backFace=*/false, /*darken=*/0.35f);

    // 5. Shadow on the prev page flat portion
    drawFlatShadow(mirrorFoldX);
}

// ---------------------------------------------------------------------------
// Shadow helpers – drawn as simple colored quads using the flat shader
// with a pre-created 1x2 gradient texture baked in.
// For simplicity we use a two-vertex triangle strip with alpha.
// ---------------------------------------------------------------------------

// We draw shadows as a full-height vertical quad covering the shadow strip,
// using a simple RGBA gradient built at draw time via a tiny 2-pixel texture.

void PageCurlRenderer::drawRevealShadow(float foldX) {
    // Shadow: dark strip just to the RIGHT of foldX, fading rightward
    // Width ~3% of page
    const float shadowW = 0.04f;
    const float x0 = foldX;
    const float x1 = foldX + shadowW;

    glUseProgram(mFlatProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);  // no texture; we draw with a blend trick

    // Build a tiny 1D gradient: 1px dark (left) → 1px transparent (right)
    // Upload to slot 0 temporarily — but we already use those for pages.
    // Use raw vertex colours instead via a dedicated shadow draw:
    //
    // Actually simplest: draw a quad with blending, no texture, using
    // gl_FragCoord-based alpha in the flat shader would require shader changes.
    //
    // Instead: use a 2x1 RGBA texture gradient for the shadow strip.
    //
    // We reuse mFlatProgram but bind a tiny temporary texture.
    uint8_t shadowTex[2 * 4] = {
        0, 0, 0, 90,   // left edge: semi-transparent black
        0, 0, 0, 0,    // right edge: transparent
    };
    GLuint tempTex;
    glGenTextures(1, &tempTex);
    glBindTexture(GL_TEXTURE_2D, tempTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, shadowTex);
    glUniform1i(mUFlatTex, 0);

    // Draw a quad [x0,x1] x [0,1]
    float verts[] = {
        x0, 0.0f, 0.0f, 0.0f,
        x1, 0.0f, 1.0f, 0.0f,
        x0, 1.0f, 0.0f, 1.0f,
        x1, 1.0f, 1.0f, 1.0f,
    };
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    const GLsizei stride = 4 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, verts);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, verts + 2);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDeleteTextures(1, &tempTex);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void PageCurlRenderer::drawFlatShadow(float foldX) {
    // Shadow: dark strip to the LEFT of foldX, fading leftward
    const float shadowW = 0.03f;
    const float x0 = foldX - shadowW;
    const float x1 = foldX;

    uint8_t shadowTex[2 * 4] = {
        0, 0, 0, 0,    // left edge: transparent
        0, 0, 0, 60,   // right edge: semi-transparent black
    };
    GLuint tempTex;
    glGenTextures(1, &tempTex);
    glBindTexture(GL_TEXTURE_2D, tempTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, shadowTex);

    glUseProgram(mFlatProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tempTex);
    glUniform1i(mUFlatTex, 0);

    float verts[] = {
        x0, 0.0f, 0.0f, 0.0f,
        x1, 0.0f, 1.0f, 0.0f,
        x0, 1.0f, 0.0f, 1.0f,
        x1, 1.0f, 1.0f, 1.0f,
    };
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    const GLsizei stride = 4 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, verts);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, verts + 2);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDeleteTextures(1, &tempTex);
    glBindTexture(GL_TEXTURE_2D, 0);
}
