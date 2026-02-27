#pragma once

#include <GLES3/gl3.h>
#include <cstdint>
#include <vector>

class PageCurlRenderer {
public:
    // Texture slots
    static constexpr int TEX_CURRENT = 0;
    static constexpr int TEX_NEXT    = 1;
    static constexpr int TEX_PREV    = 2;

    // Cylinder radius as a fraction of page width (~8%)
    static constexpr float CURL_RADIUS = 0.08f;

    // Mesh subdivisions along x-axis for smooth cylinder
    static constexpr int MESH_COLS = 64;
    static constexpr int MESH_ROWS = 2;

    PageCurlRenderer();
    ~PageCurlRenderer();

    // Called on the GL thread when surface is created
    void onSurfaceCreated();

    // Called on the GL thread when surface size changes
    void onSurfaceChanged(int width, int height);

    // Upload a bitmap to a texture slot (RGBA pixels, row-major top-to-bottom)
    // Called on the GL thread. width/height are pixel dimensions.
    void setTexture(int slot, const uint8_t* rgba, int texWidth, int texHeight);

    // Draw a forward curl: current page curls from right to left, revealing next page.
    // foldX is the normalized fold-line x position [0.0, 1.0]; 1.0 = no curl, 0.0 = fully turned.
    void drawForward(float foldX);

    // Draw a backward curl: previous page slides in from left, covering current page.
    // foldX is the normalized fold-line x position [0.0, 1.0]; 0.0 = no curl, 1.0 = fully turned.
    void drawBackward(float foldX);

    // Release all GL resources. Must be called on the GL thread.
    void release();

private:
    GLuint mProgram      = 0;
    GLuint mFlatProgram  = 0;
    GLuint mTextures[3]  = {0, 0, 0};
    GLuint mVBO          = 0;
    GLuint mEBO          = 0;
    int    mIndexCount   = 0;
    int    mSurfaceW     = 0;
    int    mSurfaceH     = 0;

    // Uniform locations for mProgram (curl)
    GLint mUFoldX    = -1;
    GLint mURadius   = -1;
    GLint mUBackFace = -1;
    GLint mUDarken   = -1;
    GLint mUTex      = -1;

    // Uniform location for mFlatProgram
    GLint mUFlatTex  = -1;

    void buildMesh();
    GLuint compileShader(GLenum type, const char* src);
    GLuint createProgram(const char* vertSrc, const char* fragSrc);

    // Draw a flat (uncurled) full-page quad using mFlatProgram
    void drawFlat(int texSlot);

    // Draw the page mesh with cylindrical curl applied in the vertex shader.
    // backFace: render with GL_FRONT culled (show back side of the curl).
    // darken:   0.0 = no darkening, 1.0 = max darkening on back face.
    void drawCurl(int texSlot, float foldX, bool backFace, float darken);

    // Draw a vertical shadow strip to the right of foldX (on the revealed page).
    void drawRevealShadow(float foldX);

    // Draw a vertical shadow strip to the left of foldX (on the current page).
    void drawFlatShadow(float foldX);
};
