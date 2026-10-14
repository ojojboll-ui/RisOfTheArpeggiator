#pragma once

#include <vector>

#include "Math.h"
#include "Neon.h"

/**
    The floor: a mesh of point masses on springs.

    Each point is tied to its four neighbours and, weakly, to where it rests,
    and the edge points do not move at all. Explosions push points out, black
    holes pull them in, and the springs carry the dent outward as a ripple and
    settle it back — the effect that makes Geometry Wars feel like it happens
    on a surface rather than in a void.

    The kick reaches it too: on every beat the whole mesh brightens and the
    ship's position gets a small push, so the floor pumps with the track.
*/
namespace np
{
    class Grid
    {
    public:
        void build (float left, float top, float right, float bottom, float spacing)
        {
            x0 = left; y0 = top;
            cols = (int) ((right - left) / spacing) + 1;
            rows = (int) ((bottom - top) / spacing) + 1;
            dx = (right - left) / (float) (cols - 1);
            dy = (bottom - top) / (float) (rows - 1);

            points.assign ((size_t) (cols * rows), {});
            for (int r = 0; r < rows; ++r)
                for (int c = 0; c < cols; ++c)
                {
                    auto& p = at (c, r);
                    p.rest = p.pos = { x0 + c * dx, y0 + r * dy };
                    p.fixed = r == 0 || c == 0 || r == rows - 1 || c == cols - 1;
                }
        }

        /** Out from a point, falling off with distance. */
        void push (Vec2 centre, float force, float radius)
        {
            forEachNear (centre, radius, [&] (Point& p, Vec2 d, float dist)
            {
                const float k = force * (1.0f - dist / radius);
                p.vel += (dist > 1.0f ? d / dist : Vec2 { 0, 0 }) * k;
            });
        }

        /** In towards a point — the black hole's pull, and a little swirl. */
        void pull (Vec2 centre, float force, float radius)
        {
            forEachNear (centre, radius, [&] (Point& p, Vec2 d, float dist)
            {
                const float k = force * (1.0f - dist / radius);
                if (dist > 4.0f) p.vel -= (d / dist) * k + (d.perp() / dist) * (k * 0.25f);
            });
        }

        void update (float dt)
        {
            // Two half steps keep the stiff springs stable at 60 Hz.
            for (int pass = 0; pass < 2; ++pass)
            {
                const float h = dt * 0.5f;
                const float damp = std::pow (0.04f, h);

                for (int r = 1; r < rows - 1; ++r)
                    for (int c = 1; c < cols - 1; ++c)
                    {
                        auto& p = at (c, r);
                        Vec2 f = (p.rest - p.pos) * kAnchor;

                        f += spring (p, at (c - 1, r), dx);
                        f += spring (p, at (c + 1, r), dx);
                        f += spring (p, at (c, r - 1), dy);
                        f += spring (p, at (c, r + 1), dy);

                        p.vel += f * h;
                    }

                for (auto& p : points)
                {
                    if (p.fixed) { p.vel = {}; continue; }
                    p.vel *= damp;
                    p.pos += p.vel * h;
                }
            }
        }

        /** `pulse` 0..1 brightens the whole mesh, `tint` is the song's colour. */
        void draw (Neon& neon, Color tint, float pulse) const
        {
            const float base = 0.10f + 0.16f * pulse;

            for (int r = 0; r < rows; ++r)
                for (int c = 0; c < cols; ++c)
                {
                    const auto& p = at (c, r);
                    const bool major = (c % 4 == 0) || (r % 4 == 0);

                    if (c + 1 < cols)
                    {
                        const auto& q = at (c + 1, r);
                        const float stretch = std::min (1.0f, std::abs ((q.pos - p.pos).length() - dx) / dx * 3.0f);
                        const bool rowMajor = r % 4 == 0;
                        neon.line (p.pos, q.pos, tint.withAlpha (base * (rowMajor ? 1.6f : 0.8f) + stretch * 0.5f),
                                   rowMajor ? 1.9f : 1.1f);   // thinner, since Neon now thickens every line 1.5x
                    }
                    if (r + 1 < rows)
                    {
                        const auto& q = at (c, r + 1);
                        const float stretch = std::min (1.0f, std::abs ((q.pos - p.pos).length() - dy) / dy * 3.0f);
                        const bool colMajor = c % 4 == 0;
                        neon.line (p.pos, q.pos, tint.withAlpha (base * (colMajor ? 1.6f : 0.8f) + stretch * 0.5f),
                                   colMajor ? 1.9f : 1.1f);
                    }
                    (void) major;
                }
        }

    private:
        struct Point { Vec2 pos, rest, vel; bool fixed = false; };

        static constexpr float kAnchor = 9.0f;
        static constexpr float kStiff  = 140.0f;

        Point& at (int c, int r) { return points[(size_t) (r * cols + c)]; }
        const Point& at (int c, int r) const { return points[(size_t) (r * cols + c)]; }

        Vec2 spring (const Point& p, const Point& q, float restLen) const
        {
            const Vec2 d = q.pos - p.pos;
            const float len = d.length();
            if (len < 1.0e-4f) return {};
            return d / len * ((len - restLen) * kStiff);
        }

        template <typename F>
        void forEachNear (Vec2 centre, float radius, F&& f)
        {
            const int c0 = std::max (1, (int) ((centre.x - radius - x0) / dx));
            const int c1 = std::min (cols - 2, (int) ((centre.x + radius - x0) / dx) + 1);
            const int r0 = std::max (1, (int) ((centre.y - radius - y0) / dy));
            const int r1 = std::min (rows - 2, (int) ((centre.y + radius - y0) / dy) + 1);

            for (int r = r0; r <= r1; ++r)
                for (int c = c0; c <= c1; ++c)
                {
                    auto& p = at (c, r);
                    const Vec2 d = p.pos - centre;
                    const float dist = d.length();
                    if (dist < radius) f (p, d, dist);
                }
        }

        std::vector<Point> points;
        int cols = 0, rows = 0;
        float x0 = 0, y0 = 0, dx = 1, dy = 1;
    };
}
