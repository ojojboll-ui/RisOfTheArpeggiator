#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <vector>

#include "Math.h"

/**
    Everything is light.

    The whole picture is drawn additively: every line is a quad textured with a
    soft cross-section (a hot core inside a wide falloff), every flash is a
    radial blob, and where things overlap they get brighter, as light does. Then
    the frame is bloomed — shrunk by halves four times with a small blur at each
    step and added back on top, which is what makes neon look like it is giving
    off light rather than painted on.

    All of this is plain SDL_Renderer: two textures, SDL_RenderGeometry and
    render targets. No shaders, so it runs on whatever backend SDL picks. The
    targets are half-float when the backend has them, so faint glow does not
    band, and eight-bit otherwise.

    World coordinates are a fixed 1920 × 1080 field, fitted to the window and
    letterboxed; the renderer owns that transform, so the game never sees pixels.
*/
namespace np
{
    constexpr float kWorldW = 1920.0f;
    constexpr float kWorldH = 1080.0f;

    class Neon
    {
    public:
        bool init (SDL_Renderer* r);
        void shutdown();

        /** Starts a frame at the window's current pixel size. `shake` offsets the
            camera in world units. */
        void begin (Vec2 shake = {});

        /** Draws the frame and bloom to the window. Does not present. */
        void end (float bloom);

        // World-space primitives ------------------------------------------------
        void line (Vec2 a, Vec2 b, Color c, float width);
        void polyline (const Vec2* pts, int n, bool closed, Color c, float width);

        /** A line with a hot white core — for the things you must not lose
            sight of. Costs two lines. */
        void neonLine (Vec2 a, Vec2 b, Color c, float width);
        void neonPoly (const Vec2* pts, int n, bool closed, Color c, float width);

        void glow (Vec2 p, float radius, Color c);
        void circle (Vec2 p, float radius, Color c, float width, int segments = 32);

        /** Vector text. `size` is the cap height in world units. Align: 0 left,
            0.5 centre, 1 right. Returns the width drawn. */
        float text (const char* utf8, Vec2 pos, float size, Color c, float align = 0.0f, float width = 0.0f);
        static float textWidth (const char* utf8, float size);

        float pixelScale() const { return scale; }

        /** The camera for what follows: world point `centre` in the middle of
            the view, magnified by `zoom`. The HUD is drawn after resetCamera(). */
        void setCamera (Vec2 centre, float zoom, Vec2 pivot = { kWorldW * 0.5f, kWorldH * 0.5f });

        /** Lines and glows drawn between these are clipped to a world-space
            rectangle (as it lies on screen without the camera) — the field, so
            a zoomed field never spills over its border. */
        void beginClip (float l, float t, float r, float b);
        void endClip();

        /** What is drawn between these — the HUD — skips the slow-motion
            effects (trails, radial blur, aberration) but still glows. */
        void beginOverlay() { overlay = true; }
        void endOverlay()   { overlay = false; }
        void resetCamera();

        /** A window pixel back to the world, through the last game camera. */
        Vec2 screenToWorld (Vec2 pixel) const;

        /** Slow motion's look, set before end(): `trails` is how much of the
            last frame lingers this frame (0..0.95, already adjusted for the
            frame rate), `aberration` 0..1 how far the colour channels split. */
        float trails = 0.0f, aberration = 0.0f;

        /** Radial blur, 0..1, streaking out from `radialCentre` (a world point
            under the game camera). */
        float radialBlur = 0.0f;
        Vec2 radialCentre { kWorldW * 0.5f, kWorldH * 0.5f };

        /** Every line is drawn this much wider than asked; text gets textBoost
            on top. Raised 2026-10-08: thin lines were hard to read. */
        float thickness = 1.5f, textBoost = 1.3f;

        bool forceLdr = false;      // eight-bit targets even where half-float exists
        bool skipBloom = false;

        // Diagnostics: quads and pixels covered last frame.
        int statQuads = 0; double statArea = 0.0, statMaxArea = 0.0;

    private:
        Vec2 toPixels (Vec2 w) const
        {
            const Vec2 z = (w - camCentre) * camZoom + camPivot;
            return { offset.x + (z.x + shakeOffset.x) * scale, offset.y + (z.y + shakeOffset.y) * scale };
        }

        Vec2 camCentre { kWorldW * 0.5f, kWorldH * 0.5f }, gameCentre { kWorldW * 0.5f, kWorldH * 0.5f };
        float camZoom = 1.0f, gameZoom = 1.0f;
        Vec2 camPivot { kWorldW * 0.5f, kWorldH * 0.5f }, gamePivot { kWorldW * 0.5f, kWorldH * 0.5f };
        bool clipping = false, overlay = false;
        SDL_Texture* hud = nullptr;
        std::vector<SDL_Vertex> lineVertsHud, glowVertsHud;
        std::vector<int> lineIdxHud, glowIdxHud;
        float clipL = 0, clipT = 0, clipR = 0, clipB = 0;
        std::vector<SDL_Vertex> lineVertsClip, glowVertsClip;
        std::vector<int> lineIdxClip, glowIdxClip;
        SDL_Texture* history = nullptr;      // last frame, for trails
        SDL_BlendMode maxBlend = SDL_BLENDMODE_INVALID, subtractBlend = SDL_BLENDMODE_INVALID;

        void ensureTargets (int w, int h);
        SDL_Texture* makeTarget (int w, int h);
        void pushQuad (std::vector<SDL_Vertex>& v, std::vector<int>& idx,
                       Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, Color c,
                       Vec2 t0, Vec2 t1, Vec2 t2, Vec2 t3);

        SDL_Renderer* renderer = nullptr;
        SDL_Texture* lineTex = nullptr;
        SDL_Texture* glowTex = nullptr;
        SDL_Texture* scene = nullptr;
        std::array<SDL_Texture*, 5> blur {};
        std::array<int, 5> blurW {}, blurH {};
        SDL_PixelFormat targetFormat = SDL_PIXELFORMAT_UNKNOWN;

        int pixelW = 0, pixelH = 0;
        float scale = 1.0f;
        Vec2 offset, shakeOffset;

        std::vector<SDL_Vertex> lineVerts, glowVerts;
        std::vector<int> lineIdx, glowIdx;
    };
}
