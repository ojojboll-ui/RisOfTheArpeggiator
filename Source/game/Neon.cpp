#include "Neon.h"

#include <cstring>

namespace np
{
    //==========================================================================
    // Vector font. Glyphs live on a 4 × 6 grid, y down; each segment is four
    // letters x1 y1 x2 y2 where 'a' is 0 and every letter after it is half a
    // unit more ('m' is 6). Upper-case letters reach above the cap line for the
    // Swedish diacritics: 'A' is -0.5, 'B' -1, 'C' -1.5.

    namespace
    {
        const char* glyph (unsigned cp)
        {
            switch (cp)
            {
                case 'A': return "amae aeea eaie ieim aiii";
                case 'B': return "aaam aaga gaic icie iegg aggg ggii iiik ikgm gmam";
                case 'C': return "iaaa aaam amim";
                case 'D': return "aaam aaea eaie ieii iiem emam";
                case 'E': return "iaaa aaam amim aggg";
                case 'F': return "iaaa aaam aggg";
                case 'G': return "iaaa aaam amim imig igeg";
                case 'H': return "aaam iaim agig";
                case 'I': return "eaem caga cmgm";
                case 'J': return "iaim imam amai";
                case 'K': return "aaam agia agim";
                case 'L': return "aaam amim";
                case 'M': return "amaa aaeg egia iaim";
                case 'N': return "amaa aaim imia";
                case 'O': return "aaia iaim imam amaa";
                case 'P': return "amaa aaia iaig igag";
                case 'Q': return "aaia iaim imam amaa eiim";
                case 'R': return "amaa aaia iaig igag egim";
                case 'S': return "iaaa aaag agig igim imam";
                case 'T': return "aaia eaem";
                case 'U': return "aaam amim imia";
                case 'V': return "aaem emia";
                case 'W': return "aacm cmeg eggm gmia";
                case 'X': return "aaim iaam";
                case 'Y': return "aaeg iaeg egem";
                case 'Z': return "aaia iaam amim";
                case 'm': return "amae aeie ieim eeem";
                case '0': return "aaia iaim imam amaa";
                case '1': return "eaem ccea cmgm";
                case '2': return "aaia iaig igag agam amim";
                case '3': return "aaia iaim imam cgig";
                case '4': return "aaag agig iaim";
                case '5': return "iaaa aaag agig igim imam";
                case '6': return "iaaa aaam amim imig igag";
                case '7': return "aaia iacm";
                case '8': return "aaia iaim imam amaa agig";
                case '9': return "igag agaa aaia iaim imam";
                case '-': return "cggg";
                case '+': return "ecek agig";
                case '.': return "elem";
                case ':': return "edee ejek";
                case '!': return "eaei elem";
                case '/': return "amia";
                case '#': return "dcck hcgk beie aihi";
                case '(': return "eacc ccck ckem";
                case ')': return "eagc gcgk gkem";
                case 0xB7: return "egfg";                                         // ·
                case 0xC5: return "amae aeea eaie ieim aiii eCfB fBeA eAdB dBeC"; // Å
                case 0xC4: return "amae aeea eaie ieim aiii cBcA gBgA";           // Ä
                case 0xD6: return "aaia iaim imam amaa cBcA gBgA";                // Ö
                default:   return nullptr;
            }
        }

        float decode (char c)
        {
            if (c >= 'a' && c <= 'z') return (float) (c - 'a') * 0.5f;
            return -(float) (c - 'A' + 1) * 0.5f;
        }

        /** Reads one code point and folds lower case to upper — except m, which
            chord names need. */
        unsigned next (const unsigned char*& s)
        {
            unsigned cp = *s++;

            if (cp >= 0xC0 && (*s & 0xC0) == 0x80)
            {
                cp = ((cp & 0x1F) << 6) | (*s++ & 0x3F);
                if (cp == 0xE5) cp = 0xC5;
                if (cp == 0xE4) cp = 0xC4;
                if (cp == 0xF6) cp = 0xD6;
            }
            else if (cp >= 'a' && cp <= 'z' && cp != 'm')
            {
                cp -= 32;
            }

            return cp;
        }

        constexpr float kAdvance = 5.5f;
        constexpr float kSpace   = 4.0f;
    }

    float Neon::textWidth (const char* utf8, float size)
    {
        const float u = size / 6.0f;
        float w = 0.0f;
        auto* s = (const unsigned char*) utf8;
        while (*s)
        {
            const unsigned cp = next (s);
            w += (cp == ' ' ? kSpace : kAdvance) * u;
        }
        return w > 0.0f ? w - 1.5f * u : 0.0f;
    }

    float Neon::text (const char* utf8, Vec2 pos, float size, Color c, float align, float width)
    {
        const float u = size / 6.0f;
        const float total = textWidth (utf8, size);
        const float lw = (width > 0.0f ? width : std::max (1.5f, size * 0.09f)) * textBoost;

        Vec2 pen { pos.x - total * align, pos.y };
        auto* s = (const unsigned char*) utf8;

        while (*s)
        {
            const unsigned cp = next (s);

            if (const char* g = glyph (cp))
            {
                for (const char* p = g; p[0] && p[1] && p[2] && p[3]; )
                {
                    const Vec2 a { pen.x + decode (p[0]) * u, pen.y + decode (p[1]) * u };
                    const Vec2 b { pen.x + decode (p[2]) * u, pen.y + decode (p[3]) * u };
                    line (a, b, c, lw);
                    p += 4;
                    while (*p == ' ') ++p;
                }
            }

            pen.x += (cp == ' ' ? kSpace : kAdvance) * u;
        }

        return total;
    }

    //==========================================================================

    bool Neon::init (SDL_Renderer* r)
    {
        renderer = r;

        // The line's cross-section: a hot core and a wide soft falloff. Packed in
        // alpha, since the additive blend multiplies colour by it.
        constexpr int lw = 4, lh = 64;
        std::vector<std::uint32_t> px ((size_t) (lw * lh));
        for (int y = 0; y < lh; ++y)
        {
            const float d = std::abs (((float) y + 0.5f) / (float) lh * 2.0f - 1.0f);   // 0 centre, 1 edge
            const float core = std::clamp ((0.34f - d) / 0.10f, 0.0f, 1.0f);   // 0.22 until 2026-10-08: a fuller core reads better
            const float halo = std::exp (-d * d * 6.0f) * 0.6f * (1.0f - d);
            const auto a = (std::uint32_t) std::lround (std::clamp (core + halo, 0.0f, 1.0f) * 255.0f);
            for (int x = 0; x < lw; ++x) px[(size_t) (y * lw + x)] = 0xFFFFFF00u | a;   // RGBA8888
        }
        lineTex = SDL_CreateTexture (r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, lw, lh);
        if (! lineTex) return false;
        SDL_UpdateTexture (lineTex, nullptr, px.data(), lw * 4);
        SDL_SetTextureBlendMode (lineTex, SDL_BLENDMODE_ADD);
        SDL_SetTextureScaleMode (lineTex, SDL_SCALEMODE_LINEAR);

        constexpr int gs = 64;
        px.assign ((size_t) (gs * gs), 0);
        for (int y = 0; y < gs; ++y)
            for (int x = 0; x < gs; ++x)
            {
                const float dx = ((float) x + 0.5f) / gs * 2.0f - 1.0f;
                const float dy = ((float) y + 0.5f) / gs * 2.0f - 1.0f;
                const float d2 = dx * dx + dy * dy;
                const float v = d2 >= 1.0f ? 0.0f : std::exp (-d2 * 4.0f) * (1.0f - d2);
                px[(size_t) (y * gs + x)] = 0xFFFFFF00u | (std::uint32_t) std::lround (v * 255.0f);
            }
        glowTex = SDL_CreateTexture (r, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, gs, gs);
        if (! glowTex) return false;
        SDL_UpdateTexture (glowTex, nullptr, px.data(), gs * 4);
        SDL_SetTextureBlendMode (glowTex, SDL_BLENDMODE_ADD);
        SDL_SetTextureScaleMode (glowTex, SDL_SCALEMODE_LINEAR);

        // max(src, dst): trails that never brighten what stands still.
        maxBlend = SDL_ComposeCustomBlendMode (SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_MAXIMUM,
                                               SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_MAXIMUM);

        // dst - src on colour, alpha kept: takes a fixed floor off the history.
        subtractBlend = SDL_ComposeCustomBlendMode (SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT,
                                                    SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT);   // SDL wants one operation for both

        lineVerts.reserve (1 << 16);
        lineIdx.reserve (1 << 17);
        return true;
    }

    void Neon::shutdown()
    {
        for (auto* t : { lineTex, glowTex, scene, history, hud }) if (t) SDL_DestroyTexture (t);
        history = hud = nullptr;
        for (auto* t : blur) if (t) SDL_DestroyTexture (t);
        lineTex = glowTex = scene = nullptr;
        blur.fill (nullptr);
    }

    SDL_Texture* Neon::makeTarget (int w, int h)
    {
        if (targetFormat == SDL_PIXELFORMAT_UNKNOWN && forceLdr) targetFormat = SDL_PIXELFORMAT_RGBA8888;
        if (targetFormat == SDL_PIXELFORMAT_UNKNOWN)
        {
            // Half-float if the backend can render into it, so a faint glow
            // keeps its gradient through four rounds of dimming.
            if (auto* t = SDL_CreateTexture (renderer, SDL_PIXELFORMAT_RGBA64_FLOAT, SDL_TEXTUREACCESS_TARGET, w, h))
            {
                targetFormat = SDL_PIXELFORMAT_RGBA64_FLOAT;
                SDL_SetTextureScaleMode (t, SDL_SCALEMODE_LINEAR);
                return t;
            }
            targetFormat = SDL_PIXELFORMAT_RGBA8888;
            SDL_Log ("Neonpuls: float render targets unavailable, using 8-bit");
        }

        auto* t = SDL_CreateTexture (renderer, targetFormat, SDL_TEXTUREACCESS_TARGET, w, h);
        if (t) SDL_SetTextureScaleMode (t, SDL_SCALEMODE_LINEAR);
        return t;
    }

    void Neon::ensureTargets (int w, int h)
    {
        if (w == pixelW && h == pixelH && scene) return;

        for (auto* t : { scene, history, hud }) if (t) SDL_DestroyTexture (t);
        for (auto*& t : blur) { if (t) SDL_DestroyTexture (t); t = nullptr; }

        pixelW = w; pixelH = h;
        scene = makeTarget (w, h);
        history = makeTarget (w, h);
        hud = makeTarget (w, h);
        if (history)
        {
            SDL_SetRenderTarget (renderer, history);
            SDL_SetRenderDrawColor (renderer, 0, 0, 0, 255);
            SDL_RenderClear (renderer);
            SDL_SetRenderTarget (renderer, nullptr);
        }

        int bw = w, bh = h;
        for (size_t i = 0; i < blur.size(); ++i)
        {
            bw = std::max (1, bw / 2);
            bh = std::max (1, bh / 2);
            blur[i] = makeTarget (bw, bh);
            blurW[i] = bw; blurH[i] = bh;
        }
    }

    void Neon::begin (Vec2 shake)
    {
        int w = 0, h = 0;
        SDL_GetCurrentRenderOutputSize (renderer, &w, &h);
        ensureTargets (std::max (1, w), std::max (1, h));

        scale = std::min ((float) w / kWorldW, (float) h / kWorldH);
        offset = { ((float) w - kWorldW * scale) * 0.5f, ((float) h - kWorldH * scale) * 0.5f };
        shakeOffset = shake;
        resetCamera();
        gameCentre = camCentre; gameZoom = camZoom;

        statQuads = 0; statArea = statMaxArea = 0.0;
        lineVerts.clear(); lineIdx.clear();
        glowVerts.clear(); glowIdx.clear();
        lineVertsClip.clear(); lineIdxClip.clear();
        glowVertsClip.clear(); glowIdxClip.clear();
        lineVertsHud.clear(); lineIdxHud.clear();
        glowVertsHud.clear(); glowIdxHud.clear();
        clipping = overlay = false;
    }

    void Neon::setCamera (Vec2 centre, float zoom, Vec2 pivot)
    {
        camCentre = gameCentre = centre;
        camZoom = gameZoom = std::max (0.1f, zoom);
        camPivot = gamePivot = pivot;
    }

    void Neon::resetCamera()
    {
        camCentre = camPivot = { kWorldW * 0.5f, kWorldH * 0.5f };
        camZoom = 1.0f;
    }

    void Neon::beginClip (float l, float t, float r, float b)
    {
        clipping = true;
        clipL = l; clipT = t; clipR = r; clipB = b;
    }

    void Neon::endClip() { clipping = false; }

    Vec2 Neon::screenToWorld (Vec2 pixel) const
    {
        const Vec2 z = (pixel - offset) / scale - shakeOffset;
        return (z - gamePivot) / gameZoom + gameCentre;
    }

    void Neon::pushQuad (std::vector<SDL_Vertex>& v, std::vector<int>& idx,
                         Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, Color c,
                         Vec2 t0, Vec2 t1, Vec2 t2, Vec2 t3)
    {
        const int base = (int) v.size();
        {
            const double area = std::abs ((double) ((p1 - p0).x * (p2 - p0).y - (p1 - p0).y * (p2 - p0).x));
            ++statQuads; statArea += area; statMaxArea = std::max (statMaxArea, area);
        }
        const SDL_FColor col { c.r, c.g, c.b, std::clamp (c.a, 0.0f, 1.0f) };
        v.push_back ({ { p0.x, p0.y }, col, { t0.x, t0.y } });
        v.push_back ({ { p1.x, p1.y }, col, { t1.x, t1.y } });
        v.push_back ({ { p2.x, p2.y }, col, { t2.x, t2.y } });
        v.push_back ({ { p3.x, p3.y }, col, { t3.x, t3.y } });
        idx.insert (idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    }

    void Neon::line (Vec2 a, Vec2 b, Color c, float width)
    {
        if (c.a <= 0.004f) return;

        const Vec2 pa = toPixels (a), pb = toPixels (b);
        Vec2 d = pb - pa;
        const float len = d.length();
        const float hw = std::max (1.4f, width * thickness * scale * camZoom * 0.5f);

        d = len > 1.0e-4f ? d / len : Vec2 { 1.0f, 0.0f };
        const Vec2 n = d.perp() * hw;
        const Vec2 e = d * (hw * 0.5f);

        pushQuad (overlay ? lineVertsHud : (clipping ? lineVertsClip : lineVerts),
                  overlay ? lineIdxHud   : (clipping ? lineIdxClip   : lineIdx),
                  pa - n - e, pa + n - e, pb + n + e, pb - n + e, c,
                  { 0.5f, 0.0f }, { 0.5f, 1.0f }, { 0.5f, 1.0f }, { 0.5f, 0.0f });
    }

    void Neon::polyline (const Vec2* pts, int n, bool closed, Color c, float width)
    {
        for (int i = 0; i + 1 < n; ++i) line (pts[i], pts[i + 1], c, width);
        if (closed && n > 2) line (pts[n - 1], pts[0], c, width);
    }

    void Neon::neonLine (Vec2 a, Vec2 b, Color c, float width)
    {
        line (a, b, c, width);
        line (a, b, Color { 1.0f, 1.0f, 1.0f, c.a * 0.4f }, width * 0.34f);
    }

    void Neon::neonPoly (const Vec2* pts, int n, bool closed, Color c, float width)
    {
        polyline (pts, n, closed, c, width);
        polyline (pts, n, closed, Color { 1.0f, 1.0f, 1.0f, c.a * 0.3f }, width * 0.32f);
    }

    void Neon::glow (Vec2 p, float radius, Color c)
    {
        if (c.a <= 0.004f) return;
        const Vec2 q = toPixels (p);
        const float r = radius * scale * camZoom;
        pushQuad (overlay ? glowVertsHud : (clipping ? glowVertsClip : glowVerts),
                  overlay ? glowIdxHud   : (clipping ? glowIdxClip   : glowIdx),
                  { q.x - r, q.y - r }, { q.x + r, q.y - r }, { q.x + r, q.y + r }, { q.x - r, q.y + r }, c,
                  { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 });
    }

    void Neon::circle (Vec2 p, float radius, Color c, float width, int segments)
    {
        Vec2 prev = p + Vec2 { radius, 0.0f };
        for (int i = 1; i <= segments; ++i)
        {
            const Vec2 cur = p + fromAngle (kTau * (float) i / (float) segments, radius);
            line (prev, cur, c, width);
            prev = cur;
        }
    }

    void Neon::end (float bloom)
    {
        // 1. The scene, additively, onto black.
        SDL_SetRenderTarget (renderer, scene);
        SDL_SetRenderDrawColor (renderer, 0, 0, 0, 255);
        SDL_RenderClear (renderer);

        // The clipped layer first: the field, inside its border.
        if (! glowVertsClip.empty() || ! lineVertsClip.empty())
        {
            const SDL_Rect clip { (int) std::floor (offset.x + clipL * scale), (int) std::floor (offset.y + clipT * scale),
                                  (int) std::ceil ((clipR - clipL) * scale), (int) std::ceil ((clipB - clipT) * scale) };
            SDL_SetRenderClipRect (renderer, &clip);
            if (! glowVertsClip.empty())
                SDL_RenderGeometry (renderer, glowTex, glowVertsClip.data(), (int) glowVertsClip.size(), glowIdxClip.data(), (int) glowIdxClip.size());
            if (! lineVertsClip.empty())
                SDL_RenderGeometry (renderer, lineTex, lineVertsClip.data(), (int) lineVertsClip.size(), lineIdxClip.data(), (int) lineIdxClip.size());
            SDL_SetRenderClipRect (renderer, nullptr);
        }

        if (! glowVerts.empty())
            SDL_RenderGeometry (renderer, glowTex, glowVerts.data(), (int) glowVerts.size(), glowIdx.data(), (int) glowIdx.size());
        if (! lineVerts.empty())
            SDL_RenderGeometry (renderer, lineTex, lineVerts.data(), (int) lineVerts.size(), lineIdx.data(), (int) lineIdx.size());

        // Trails: last frame, dimmed, folded in with max() — what moves leaves
        // a fading wake, what stands still keeps its brightness. Then this
        // frame becomes the next one's last frame.
        if (history)
        {
            if (trails > 0.01f && SDL_SetTextureBlendMode (history, maxBlend))
            {
                const float k = std::clamp (trails, 0.0f, 0.95f);
                SDL_SetTextureColorModFloat (history, k, k, k);
                SDL_RenderTexture (renderer, history, nullptr, nullptr);
                SDL_SetTextureColorModFloat (history, 1.0f, 1.0f, 1.0f);
            }

            SDL_SetRenderTarget (renderer, history);
            SDL_SetTextureBlendMode (scene, SDL_BLENDMODE_NONE);
            SDL_RenderTexture (renderer, scene, nullptr, nullptr);

            // A small floor comes off the history every frame. Scaling alone
            // never reaches zero, and at 165 fps the faint remains of
            // everything that ever moved hung over the field as a grey haze.
            if (trails > 0.01f && SDL_SetRenderDrawBlendMode (renderer, subtractBlend))
            {
                SDL_SetRenderDrawColorFloat (renderer, 0.02f, 0.02f, 0.02f, 1.0f);
                SDL_RenderFillRect (renderer, nullptr);
                SDL_SetRenderDrawBlendMode (renderer, SDL_BLENDMODE_NONE);
            }

            SDL_SetRenderTarget (renderer, scene);
        }

        // The overlay on its own target, untouched by the effects.
        const bool hasHud = hud && (! lineVertsHud.empty() || ! glowVertsHud.empty());
        if (hasHud)
        {
            SDL_SetRenderTarget (renderer, hud);
            SDL_SetRenderDrawColor (renderer, 0, 0, 0, 255);
            SDL_RenderClear (renderer);
            if (! glowVertsHud.empty())
                SDL_RenderGeometry (renderer, glowTex, glowVertsHud.data(), (int) glowVertsHud.size(), glowIdxHud.data(), (int) glowIdxHud.size());
            if (! lineVertsHud.empty())
                SDL_RenderGeometry (renderer, lineTex, lineVertsHud.data(), (int) lineVertsHud.size(), lineIdxHud.data(), (int) lineIdxHud.size());
        }

        // 2. Halve it four... five times. Each step reads the one above with
        //    four taps half a texel apart, which with linear filtering is a
        //    small tent blur on top of the box the halving already is.
        SDL_Texture* src = scene;
        for (size_t i = 0; i < (skipBloom ? 0 : blur.size()); ++i)
        {
            SDL_SetRenderTarget (renderer, blur[i]);
            SDL_SetRenderDrawColor (renderer, 0, 0, 0, 255);
            SDL_RenderClear (renderer);

            SDL_SetTextureBlendMode (src, SDL_BLENDMODE_ADD);
            SDL_SetTextureColorModFloat (src, 0.25f, 0.25f, 0.25f);

            const float w = (float) blurW[i], h = (float) blurH[i];
            static constexpr float taps[4][2] { { -0.6f, -0.6f }, { 0.6f, -0.6f }, { -0.6f, 0.6f }, { 0.6f, 0.6f } };
            for (const auto& t : taps)
            {
                const SDL_FRect dst { t[0], t[1], w, h };
                SDL_RenderTexture (renderer, src, nullptr, &dst);
            }

            if (i == 0 && hasHud)   // the HUD glows too
            {
                SDL_SetTextureBlendMode (hud, SDL_BLENDMODE_ADD);
                SDL_SetTextureColorModFloat (hud, 0.25f, 0.25f, 0.25f);
                for (const auto& t : taps)
                {
                    const SDL_FRect dst { t[0], t[1], w, h };
                    SDL_RenderTexture (renderer, hud, nullptr, &dst);
                }
                SDL_SetTextureColorModFloat (hud, 1.0f, 1.0f, 1.0f);
            }

            SDL_SetTextureColorModFloat (src, 1.0f, 1.0f, 1.0f);
            src = blur[i];
        }

        // 3. Back up the chain: each level is stretched onto the one above it
        //    and added. Climbing one octave at a time, each with linear
        //    filtering, is what keeps the glow round; stretching the smallest
        //    level straight to the window showed its pixels as squares.
        if (! skipBloom)
        {
            for (size_t i = blur.size() - 1; i >= 1; --i)
            {
                SDL_SetRenderTarget (renderer, blur[i - 1]);
                SDL_SetTextureBlendMode (blur[i], SDL_BLENDMODE_ADD);
                SDL_RenderTexture (renderer, blur[i], nullptr, nullptr);
            }
        }

        // 4. The window: the sharp scene, and the whole glow on top.
        SDL_SetRenderTarget (renderer, nullptr);
        SDL_SetRenderDrawColor (renderer, 0, 0, 0, 255);
        SDL_RenderClear (renderer);

        if (aberration > 0.001f || radialBlur > 0.001f)
        {
            // Radial blur and chromatic aberration in one: the frame is laid
            // down several times, each copy a little larger around the centre
            // (the ship, under the camera), at equal weight — streaks running
            // out from it. Each copy is split into its colour channels, red a
            // little larger and blue a little smaller, so the channels part
            // more the further they are from the centre, like a lens.
            const float w = (float) pixelW, h = (float) pixelH;
            const Vec2 z = (radialCentre - gameCentre) * gameZoom + gamePivot;
            const Vec2 c { offset.x + z.x * scale, offset.y + z.y * scale };

            const int copies = radialBlur > 0.001f ? 7 : 1;
            const float weight = 1.0f / (float) copies;
            const float ab = aberration * 0.008f;

            auto around = [&] (float s)
            {
                return SDL_FRect { c.x - c.x * s, c.y - c.y * s, w * s, h * s };
            };

            SDL_SetTextureBlendMode (scene, SDL_BLENDMODE_ADD);
            for (int i = 0; i < copies; ++i)
            {
                const float s = 1.0f + radialBlur * 0.07f * (float) i / (float) std::max (1, copies - 1);
                const SDL_FRect red = around (s * (1.0f + ab)), green = around (s), blue = around (s * (1.0f - ab));
                SDL_SetTextureColorModFloat (scene, weight, 0.0f, 0.0f); SDL_RenderTexture (renderer, scene, nullptr, &red);
                SDL_SetTextureColorModFloat (scene, 0.0f, weight, 0.0f); SDL_RenderTexture (renderer, scene, nullptr, &green);
                SDL_SetTextureColorModFloat (scene, 0.0f, 0.0f, weight); SDL_RenderTexture (renderer, scene, nullptr, &blue);
            }
            SDL_SetTextureColorModFloat (scene, 1.0f, 1.0f, 1.0f);
        }
        else
        {
            SDL_SetTextureBlendMode (scene, SDL_BLENDMODE_BLEND);
            SDL_RenderTexture (renderer, scene, nullptr, nullptr);
        }

        if (hasHud)
        {
            SDL_SetTextureBlendMode (hud, SDL_BLENDMODE_ADD);
            SDL_RenderTexture (renderer, hud, nullptr, nullptr);
        }

        if (! skipBloom)
        {
            // Colour modulation may be clamped at one, so a weight above one
            // is drawn as several passes.
            SDL_SetTextureBlendMode (blur[0], SDL_BLENDMODE_ADD);
            for (float k = 0.85f * bloom; k > 0.01f; k -= 1.0f)
            {
                const float m = std::min (1.0f, k);
                SDL_SetTextureColorModFloat (blur[0], m, m, m);
                SDL_RenderTexture (renderer, blur[0], nullptr, nullptr);
            }
            SDL_SetTextureColorModFloat (blur[0], 1.0f, 1.0f, 1.0f);
        }
    }
}
