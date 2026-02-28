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

// Curl shader: applies cylindrical transform to vertices right of the diagonal
// fold line defined by (uFoldX, uFoldSlope).
//
//   fold line x at row y  =  uFoldX + uFoldSlope * (y - 0.5)
//
// foldSlope > 0  → bottom-right corner peels first (most common for iOS style)
// foldSlope = 0  → purely vertical fold (tap animations)
static const char* kCurlVert = R"GLSL(#version 300 es
precision highp float;

in  vec2 aPos;
in  vec2 aUV;

uniform float uFoldX;      // fold line centre x [0,1]
uniform float uFoldSlope;  // fold line diagonal tilt
uniform float uRadius;     // cylinder radius [0,1]
uniform bool  uBackFace;   // true => rendering back face (mirrored)
uniform float uDarken;     // max darken amount for curl shading

out vec2  vUV;
out float vShadow;

const float PI = 3.14159265;

void main() {
    vec2  pos = aPos;
    vec2  uv  = aUV;
    float z   = 0.0;

    // Diagonal fold line x at this vertex's y position
    float foldLineX = uFoldX + uFoldSlope * (pos.y - 0.5);

    float dx = pos.x - foldLineX;
    if (dx > 0.0) {
        // CYLINDRICAL TRANSFORM relative to diagonal fold line
        float theta = min(dx / uRadius, PI);
        pos.x = foldLineX + uRadius * sin(theta);
        z     = uRadius * (1.0 - cos(theta));

        if (uBackFace) {
            // Mirror UV across the local fold line to show reverse side
            uv.x = 2.0 * foldLineX - aUV.x;
            uv.x = clamp(uv.x, 0.0, 1.0);
        }

        // Shadow peaks at theta = PI/2 (90° — edge of cylinder)
        vShadow = uDarken * sin(theta);
    } else {
        vShadow = 0.0;
    }

    // [0,1] → NDC; Y flipped because bitmap row 0 = top
    gl_Position = vec4(pos.x * 2.0 - 1.0,
                       1.0 - pos.y * 2.0,
                       -z * 0.5,
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

// Flat shader: full-page textured quad, no transforms.
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

GLuint PageCurlRenderer::createGradientTex(
        uint8_t r0, uint8_t g0, uint8_t b0, uint8_t a0,
        uint8_t r1, uint8_t g1, uint8_t b1, uint8_t a1) {
    uint8_t px[2 * 4] = { r0, g0, b0, a0,  r1, g1, b1, a1 };
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// ---------------------------------------------------------------------------
// Mesh: (MESH_COLS+1)*(MESH_ROWS+1) vertices in [0,1]x[0,1], indexed quads.
// ---------------------------------------------------------------------------

void PageCurlRenderer::buildMesh() {
    const int cols   = MESH_COLS;
    const int rows   = MESH_ROWS;
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
PageCurlRenderer::~PageCurlRenderer() { release(); }

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void PageCurlRenderer::onSurfaceCreated() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    mProgram = createProgram(kCurlVert, kCurlFrag);
    if (mProgram) {
        mUFoldX     = glGetUniformLocation(mProgram, "uFoldX");
        mUFoldSlope = glGetUniformLocation(mProgram, "uFoldSlope");
        mURadius    = glGetUniformLocation(mProgram, "uRadius");
        mUBackFace  = glGetUniformLocation(mProgram, "uBackFace");
        mUDarken    = glGetUniformLocation(mProgram, "uDarken");
        mUTex       = glGetUniformLocation(mProgram, "uTex");
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

    // Pre-allocate shadow gradient textures (created once, reused every frame)
    // Reveal shadow: dark (alpha 140) on the left → transparent on the right
    mRevealShadowTex = createGradientTex(0, 0, 0, 140,  0, 0, 0, 0);
    // Flat shadow: transparent on the left → dark (alpha 80) on the right
    mFlatShadowTex   = createGradientTex(0, 0, 0, 0,    0, 0, 0, 80);

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
    if (mVBO)             { glDeleteBuffers(1,  &mVBO);             mVBO = 0; }
    if (mEBO)             { glDeleteBuffers(1,  &mEBO);             mEBO = 0; }
    if (mProgram)         { glDeleteProgram(mProgram);              mProgram = 0; }
    if (mFlatProgram)     { glDeleteProgram(mFlatProgram);          mFlatProgram = 0; }
    if (mRevealShadowTex) { glDeleteTextures(1, &mRevealShadowTex); mRevealShadowTex = 0; }
    if (mFlatShadowTex)   { glDeleteTextures(1, &mFlatShadowTex);   mFlatShadowTex = 0; }
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

void PageCurlRenderer::drawCurl(int texSlot, float foldX, float foldSlope,
                                 bool backFace, float darken) {
    if (!mProgram || !mTextures[texSlot]) return;
    glUseProgram(mProgram);

    if (backFace) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    }

    glUniform1f(mUFoldX,     foldX);
    glUniform1f(mUFoldSlope, foldSlope);
    glUniform1f(mURadius,    CURL_RADIUS);
    glUniform1i(mUBackFace,  backFace ? 1 : 0);
    glUniform1f(mUDarken,    darken);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mTextures[texSlot]);
    glUniform1i(mUTex, 0);

    bindMeshAttribs(mVBO, mEBO);
    glDrawElements(GL_TRIANGLES, mIndexCount, GL_UNSIGNED_SHORT, nullptr);

    glDisable(GL_CULL_FACE);
}

// Draw shadow as a parallelogram that follows the diagonal fold line.
// The geometry is computed per-frame in CPU; texture is reused from pre-allocated gradient.
static void drawShadowQuad(GLuint flatProgram, GLint uFlatTex, GLuint shadowTex,
                           float x0top, float x1top,
                           float x0bot, float x1bot) {
    if (!flatProgram || !shadowTex) return;
    glUseProgram(flatProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shadowTex);
    glUniform1i(uFlatTex, 0);

    // Parallelogram: 4 corners ordered for GL_TRIANGLE_STRIP
    float verts[] = {
        x0top, 0.0f, 0.0f, 0.0f,   // top-left
        x1top, 0.0f, 1.0f, 0.0f,   // top-right
        x0bot, 1.0f, 0.0f, 1.0f,   // bottom-left
        x1bot, 1.0f, 1.0f, 1.0f,   // bottom-right
    };
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    const GLsizei stride = 4 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, verts);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, verts + 2);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindTexture(GL_TEXTURE_2D, 0);
}

void PageCurlRenderer::drawRevealShadow(float foldX, float foldSlope) {
    // Shadow strip to the RIGHT of the diagonal fold line (on the revealed page).
    // Width: 8% of page — wider than before for a more prominent depth cue.
    const float w    = 0.08f;
    float topFold    = foldX - 0.5f * foldSlope;   // fold line x at y=0 (top)
    float botFold    = foldX + 0.5f * foldSlope;   // fold line x at y=1 (bottom)
    drawShadowQuad(mFlatProgram, mUFlatTex, mRevealShadowTex,
                   topFold,     topFold + w,
                   botFold,     botFold + w);
}

void PageCurlRenderer::drawFlatShadow(float foldX, float foldSlope) {
    // Shadow strip to the LEFT of the diagonal fold line (on the uncurled page).
    // Width: 6% of page.
    const float w    = 0.06f;
    float topFold    = foldX - 0.5f * foldSlope;
    float botFold    = foldX + 0.5f * foldSlope;
    drawShadowQuad(mFlatProgram, mUFlatTex, mFlatShadowTex,
                   topFold - w, topFold,
                   botFold - w, botFold);
}

// ---------------------------------------------------------------------------
// Public draw calls
// ---------------------------------------------------------------------------

void PageCurlRenderer::drawForward(float foldX, float foldSlope) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 1. Next page flat (underneath — revealed behind the curl)
    if (mTextures[TEX_NEXT]) {
        drawFlat(TEX_NEXT);
    }

    // 2. Shadow cast on the revealed next page (follows diagonal fold line)
    drawRevealShadow(foldX, foldSlope);

    // 3. Current page — back face (paper back, lightly darkened)
    drawCurl(TEX_CURRENT, foldX, foldSlope, /*backFace=*/true, /*darken=*/0.15f);

    // 4. Current page — front face (the page content peeling away)
    drawCurl(TEX_CURRENT, foldX, foldSlope, /*backFace=*/false, /*darken=*/0.35f);

    // 5. Shadow on the still-flat portion of current page
    drawFlatShadow(foldX, foldSlope);
}

void PageCurlRenderer::drawBackward(float foldX, float foldSlope) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 1. Current page flat (underneath)
    if (mTextures[TEX_CURRENT]) {
        drawFlat(TEX_CURRENT);
    }

    // 2. Shadow on the current page
    drawRevealShadow(foldX, foldSlope);

    // 3. Previous page — back face (mirrored fold for left-side peel)
    float mirrorFoldX   = 1.0f - foldX;
    float mirrorSlope   = -foldSlope;   // negate slope when mirroring X axis
    drawCurl(TEX_PREV, mirrorFoldX, mirrorSlope, /*backFace=*/true,  /*darken=*/0.15f);

    // 4. Previous page — front face
    drawCurl(TEX_PREV, mirrorFoldX, mirrorSlope, /*backFace=*/false, /*darken=*/0.35f);

    // 5. Shadow on the prev page flat portion
    drawFlatShadow(mirrorFoldX, mirrorSlope);
}
