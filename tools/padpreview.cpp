// Renders STARFALL's pad round the wheel with a candidate in the right-hand
// slot, so a chord can be chosen by ear before the song is changed.
//
//   build.bat padpreview [outdir]
//
// Same patch (padSoftPatch), voicing (close, root from E3), filter (0.57,
// res 0.12), chorus and room as the game's held pad; no drums, no ducking.
// Clockwise: F#m, candidate, A, E — one bar each at 138 BPM, twice round.

#include "../Source/audio/Ensemble.h"
#include "../Source/audio/JunoVoice.h"
#include "../Source/audio/Reverb.h"
#include "../Source/audio/Songs.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace np::audio;

namespace
{
    constexpr double kRate = 48000.0;

    struct Chord { int pc; bool minor; const char* name; };

    void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
    {
        FILE* f = std::fopen (path.c_str(), "wb");
        if (! f) { std::printf ("cannot write %s\n", path.c_str()); return; }
        const std::uint32_t frames = (std::uint32_t) l.size(), bytes = frames * 4;
        auto u32 = [f] (std::uint32_t v) { std::fwrite (&v, 4, 1, f); };
        auto u16 = [f] (std::uint16_t v) { std::fwrite (&v, 2, 1, f); };
        std::fwrite ("RIFF", 1, 4, f); u32 (36 + bytes); std::fwrite ("WAVE", 1, 4, f);
        std::fwrite ("fmt ", 1, 4, f); u32 (16); u16 (1); u16 (2); u32 ((std::uint32_t) kRate); u32 ((std::uint32_t) kRate * 4); u16 (4); u16 (16);
        std::fwrite ("data", 1, 4, f); u32 (bytes);
        for (std::uint32_t i = 0; i < frames; ++i)
        {
            const auto a = (std::int16_t) std::lround (std::clamp (l[i], -1.0f, 1.0f) * 32767.0f);
            const auto b = (std::int16_t) std::lround (std::clamp (r[i], -1.0f, 1.0f) * 32767.0f);
            std::fwrite (&a, 2, 1, f); std::fwrite (&b, 2, 1, f);
        }
        std::fclose (f);
    }

    void render (const Chord (&wheel)[4], const std::string& path)
    {
        std::array<JunoVoice, 4> pad;
        for (auto& v : pad) { v.prepare (kRate); v.setPatch (padSoftPatch()); }
        std::array<Ensemble, 2> chorus;
        for (auto& e : chorus) { e.prepare (kRate); e.setRate (0.5f); e.setMix (0.5f); }
        Reverb room; room.prepare (kRate); room.setRoom (0.82f, 0.4f);

        const double bar = kRate * 60.0 / 138.0 * 4.0;
        const int bars = 8;
        const size_t total = (size_t) (bar * bars + kRate * 3.0);   // and three seconds of tail
        std::vector<float> outL (total), outR (total);
        std::uint64_t order = 0;
        int nextBar = 0;

        for (size_t i = 0; i < total; ++i)
        {
            // Each bar's downbeat: the next chord, held, voices that keep their
            // note left alone — as the game's padOn does.
            if (nextBar < bars && i == (size_t) std::llround (nextBar * bar))
            {
                const auto& c = wheel[nextBar % 4];
                ++nextBar;
                const int root = placeAbove (c.pc, 52);
                const int third = c.minor ? 3 : 4;
                const int notes[4] { root, root + third, root + 7, root + 12 };
                for (int k = 0; k < 4; ++k)
                {
                    auto& v = pad[(size_t) k];
                    if (v.isGateOn() && v.midiNote() == notes[k]) continue;
                    v.noteOff();
                    v.noteOn (notes[k], 0.8f, 0, ++order);
                }
            }
            if (i == (size_t) std::llround (bars * bar))
                for (auto& v : pad) v.noteOff();

            float mono = 0.0f;
            for (auto& v : pad) if (v.isActive()) mono += v.process (0.0f, 0.57f, 0.12f, 1.0f) * 0.72f * 1.2f;

            float l = 0.0f, r = 0.0f, unused = 0.0f;
            chorus[0].process (mono * 0.3f, l, unused);
            chorus[1].process (mono * 0.3f, unused, r);

            float wl = 0.0f, wr = 0.0f;
            room.process (l * 0.6f * 0.30f, r * 0.6f * 0.30f, wl, wr);

            // The game's pad bus level (3.2), and a little headroom.
            outL[i] = (l * 3.2f + wl) * 0.8f;
            outR[i] = (r * 3.2f + wr) * 0.8f;
        }

        writeWav (path, outL, outR);
        std::printf ("wrote %s\n", path.c_str());
    }
}

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";

    const Chord fsm { 6, true, "F#m" }, a { 9, false, "A" }, e { 4, false, "E" };
    const Chord candidates[] {
        { 2,  false, "D" },     // the original VI
        { 11, true,  "Bm" },    // iv
        { 11, false, "B" },     // IV, the dorian lift
        { 1,  true,  "C#m" },   // v
        { 1,  false, "C#" }     // V, from harmonic minor
    };

    int n = 1;
    for (const auto& c : candidates)
    {
        const Chord wheel[4] { fsm, c, a, e };
        // No "#" in file names: it breaks links to them.
        std::string name = c.name;
        for (size_t p; (p = name.find ('#')) != std::string::npos; ) name.replace (p, 1, "sharp-");
        if (! name.empty() && name.back() == '-') name.pop_back();
        render (wheel, dir + "/starfall-" + std::to_string (n++) + "-" + name + ".wav");
    }
    return 0;
}
