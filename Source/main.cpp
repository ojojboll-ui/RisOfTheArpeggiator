// Rise of the Arpeggiator (working name Neonpuls) — a twin-stick shooter that
// is also the band.
//
// Left stick: fly, and point at one of four chords. Right stick: aim, and play
// the arpeggio — every note is a shot. Survive and the arrangement grows from
// kick and bass to the full track; die and it loses a layer.
//
// Flags, for testing without a pad:
//   --demo           the autopilot plays (starts straight into a level)
//   --level N        1..3, which song (default 1)
//   --difficulty D   easy, normal or hard (default normal)
//   --seconds T      quit after T seconds
//   --shots DIR      save a PNG every --every seconds (default 10) into DIR
//   --wav PATH       record what the band played
//   --log PATH       write frame timing and events
//   --fullscreen

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "audio/Conductor.h"
#include "game/Autopilot.h"
#include "game/Input.h"
#include "game/Neon.h"
#include "game/Png.h"
#include "game/World.h"

using namespace np;

namespace
{
    constexpr int kSampleRate = 48000;

    struct App
    {
        SDL_Window* window = nullptr;
        SDL_Renderer* renderer = nullptr;
        SDL_AudioStream* stream = nullptr;

        std::unique_ptr<audio::Conductor> band;
        std::unique_ptr<World> world;
        std::unique_ptr<World> ambient;
        Neon neon;
        Input input;
        Autopilot pilot;

        enum class Screen { menu, game, paused } screen = Screen::menu;
        int selected = 0;
        bool autopilot = false;
        float idle = 0.0f;
        int best[3][3] {};              // [difficulty][song]
        int difficulty = 1;
        std::string savePath;

        // Test hooks
        double quitAfter = -1.0;
        std::string shotsDir, wavPath, logPath;
        double shotEvery = 10.0, nextShot = 0.0;
        int shotIndex = 0;
        FILE* log = nullptr;

        // Audio thread scratch
        std::vector<float> left, right, interleaved;
        std::vector<float> recording;
        std::atomic<bool> recordOn { false };
        std::atomic<int> maxRequest { 0 }, minRequest { 1 << 30 }, callbacks { 0 };
    };

    void SDLCALL audioCallback (void* user, SDL_AudioStream* stream, int additional, int /*total*/)
    {
        auto* app = static_cast<App*> (user);
        int frames = additional / (int) (2 * sizeof (float));
        if (frames > 0)
        {
            app->callbacks++;
            if (frames > app->maxRequest) app->maxRequest = frames;
            if (frames < app->minRequest) app->minRequest = frames;
        }

        while (frames > 0)
        {
            const int n = std::min (frames, 1024);
            app->band->render (app->left.data(), app->right.data(), n);

            for (int i = 0; i < n; ++i)
            {
                app->interleaved[(size_t) (2 * i)]     = app->left[(size_t) i];
                app->interleaved[(size_t) (2 * i + 1)] = app->right[(size_t) i];
            }

            if (app->recordOn)
                app->recording.insert (app->recording.end(), app->interleaved.begin(), app->interleaved.begin() + 2 * n);

            SDL_PutAudioStreamData (stream, app->interleaved.data(), n * 2 * (int) sizeof (float));
            frames -= n;
        }
    }

    void loadBest (App& app)
    {
        // Still the Neonpuls folder, so the best scores survive the new name.
        if (char* pref = SDL_GetPrefPath ("Oloff", "Neonpuls"))
        {
            app.savePath = std::string (pref) + "best.txt";
            SDL_free (pref);
        }
        FILE* f = nullptr;
        if (! app.savePath.empty() && fopen_s (&f, app.savePath.c_str(), "r") == 0 && f)
        {
            // Nine numbers since 2026-10-09 (three difficulties); an older file
            // has three, and those were normal.
            int v[9] {};
            const int n = std::fscanf (f, "%d %d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]);
            if (n == 9) for (int i = 0; i < 9; ++i) app.best[i / 3][i % 3] = v[i];
            else if (n >= 3) for (int i = 0; i < 3; ++i) app.best[1][i] = v[i];
            std::fclose (f);
        }
    }

    void saveBest (App& app)
    {
        FILE* f = nullptr;
        if (! app.savePath.empty() && fopen_s (&f, app.savePath.c_str(), "w") == 0 && f)
        {
            for (int dIdx = 0; dIdx < 3; ++dIdx)
                std::fprintf (f, "%d %d %d\n", app.best[dIdx][0], app.best[dIdx][1], app.best[dIdx][2]);
            std::fclose (f);
        }
    }

    void writeWav (const std::string& path, const std::vector<float>& data)
    {
        FILE* f = nullptr;
        if (fopen_s (&f, path.c_str(), "wb") != 0 || ! f) return;
        const std::uint32_t bytes = (std::uint32_t) (data.size() * 2);
        auto u32 = [f] (std::uint32_t v) { std::fwrite (&v, 4, 1, f); };
        auto u16 = [f] (std::uint16_t v) { std::fwrite (&v, 2, 1, f); };
        std::fwrite ("RIFF", 1, 4, f); u32 (36 + bytes); std::fwrite ("WAVE", 1, 4, f);
        std::fwrite ("fmt ", 1, 4, f); u32 (16); u16 (1); u16 (2); u32 (kSampleRate); u32 (kSampleRate * 4); u16 (4); u16 (16);
        std::fwrite ("data", 1, 4, f); u32 (bytes);
        for (float s : data)
        {
            const auto v = (std::int16_t) std::lround (std::clamp (s, -1.0f, 1.0f) * 32767.0f);
            std::fwrite (&v, 2, 1, f);
        }
        std::fclose (f);
    }

    void screenshot (App& app)
    {
        SDL_Surface* s = SDL_RenderReadPixels (app.renderer, nullptr);
        if (! s) return;
        SDL_Surface* rgb = SDL_ConvertSurface (s, SDL_PIXELFORMAT_RGB24);
        SDL_DestroySurface (s);
        if (! rgb) return;

        char name[512];
        std::snprintf (name, sizeof (name), "%s/shot-%02d.png", app.shotsDir.c_str(), app.shotIndex++);
        png::writeRgb (name, (const std::uint8_t*) rgb->pixels, rgb->w, rgb->h, rgb->pitch);
        SDL_DestroySurface (rgb);
    }

    //==========================================================================

    void startGame (App& app, int level, bool pilot)
    {
        app.autopilot = pilot;
        app.world = std::make_unique<World> (*app.band);
        app.world->start (level, app.difficulty);
        app.screen = App::Screen::game;
    }

    void backToMenu (App& app)
    {
        if (app.world)
        {
            const int lvl = app.world->levelIndex();
            const int dIdx = app.world->difficultyIndex();
            if (! app.autopilot && app.world->score() > app.best[dIdx][lvl])
            {
                app.best[dIdx][lvl] = app.world->score();
                saveBest (app);
            }
        }
        app.band->post ({ audio::Command::stop });
        app.world.reset();
        app.screen = App::Screen::menu;
        app.autopilot = false;
        app.idle = 0.0f;
    }

    void drawMenu (App& app, float time)
    {
        auto& n = app.neon;
        const Color accent = levelAccent (app.selected);

        app.ambient->drawAmbient (n, levelTint (app.selected));

        // Title: drawn twice, the second slightly offset, so it shimmers.
        const float pulse = 0.5f + 0.5f * std::sin (time * 2.0f);
        // Title on two lines: the long name will not fit the width at one size.
        n.text ("RISE OF THE", { kWorldW * 0.5f, 70 }, 52.0f, accent, 0.5f, 5.0f);
        n.text ("ARPEGGIATOR", { kWorldW * 0.5f, 150 }, 120.0f, accent, 0.5f, 9.0f);
        n.text ("ARPEGGIATOR", { kWorldW * 0.5f, 150 }, 120.0f, Color { 1, 1, 1, 0.35f + 0.2f * pulse }, 0.5f, 3.0f);
        n.text ("FOUR CHORDS · ONE ARPEGGIO · SURVIVE THE SONG", { kWorldW * 0.5f, 320 }, 24.0f, Color { 1, 1, 1, 0.75f }, 0.5f, 2.4f);

        for (int i = 0; i < 3; ++i)
        {
            const auto& s = audio::song (i);
            const bool on = i == app.selected;
            const Color c = levelAccent (i).withAlpha (on ? 1.0f : 0.35f);
            const float cx = kWorldW * 0.5f + (float) (i - 1) * 470.0f;
            const float top = 400.0f, bottom = 760.0f, half = 205.0f;

            const Vec2 box[4] { { cx - half, top }, { cx + half, top }, { cx + half, bottom }, { cx - half, bottom } };
            if (on) n.neonPoly (box, 4, true, c, 4.0f);
            else    n.polyline (box, 4, true, c, 2.5f);
            if (on) n.glow ({ cx, (top + bottom) * 0.5f }, 260.0f, c.withAlpha (0.10f + 0.06f * pulse));

            char buf[64];
            std::snprintf (buf, sizeof (buf), "%d", i + 1);
            n.text (buf, { cx, top + 30 }, 34.0f, c, 0.5f, 3.0f);
            const float nameSize = std::min (44.0f, 44.0f * 370.0f / std::max (1.0f, Neon::textWidth (s.name, 44.0f)));
            n.text (s.name, { cx, top + 95 + (44.0f - nameSize) * 0.5f }, nameSize, on ? Color { 1, 1, 1 } : c, 0.5f, 4.0f);
            n.text (s.tagline, { cx, top + 160 }, 20.0f, c, 0.5f, 2.2f);
            std::snprintf (buf, sizeof (buf), "%d BPM", (int) s.bpm);
            n.text (buf, { cx, top + 200 }, 18.0f, Color { 1, 1, 1, on ? 0.8f : 0.35f }, 0.5f, 2.0f);

            std::string chords;
            for (int k = 0; k < 4; ++k)
            {
                char name[8];
                audio::chordName (s, k, name, sizeof (name));
                chords += name;
                if (k < 3) chords += "  ";
            }
            n.text (chords.c_str(), { cx, top + 240 }, 22.0f, Color { 1, 1, 1, on ? 0.9f : 0.35f }, 0.5f, 2.2f);

            std::snprintf (buf, sizeof (buf), "BEST %d", app.best[app.difficulty][i]);
            n.text (buf, { cx, top + 300 }, 18.0f, c, 0.5f, 2.0f);
        }

        // Difficulty, chosen with up and down.
        for (int dIdx = 0; dIdx < 3; ++dIdx)
        {
            const bool sel = dIdx == app.difficulty;
            const float x = kWorldW * 0.5f + (float) (dIdx - 1) * 230.0f;
            n.text (difficulty (dIdx).name, { x, 786 }, sel ? 30.0f : 24.0f,
                    sel ? accent : Color { 1, 1, 1, 0.4f }, 0.5f);
            if (sel)
            {
                const float w = Neon::textWidth (difficulty (dIdx).name, 30.0f) * 0.5f + 16.0f;
                n.line ({ x - w, 830 }, { x + w, 830 }, accent, 3.0f);
            }
        }

        const Color help { 1, 1, 1, 0.75f };
        n.text ("LEFT STICK  FLY + PICK CHORD     RIGHT STICK  SHOOT + PLAY ARPEGGIO", { kWorldW * 0.5f, 862 }, 20.0f, help, 0.5f);
        n.text ("RT LB RB  BOMB    LT  SLOW MOTION    START  PAUSE    UP DOWN  DIFFICULTY", { kWorldW * 0.5f, 898 }, 20.0f, help, 0.5f);
        n.text ("KEYBOARD  WASD + ARROWS OR MOUSE   SPACE BOMB   SHIFT SLOW MOTION", { kWorldW * 0.5f, 934 }, 18.0f,
                Color { 1, 1, 1, 0.55f }, 0.5f);
        n.text (app.input.hasPad() ? "A  PLAY" : "ENTER  PLAY", { kWorldW * 0.5f, 1000 }, 34.0f,
                accent.withAlpha (0.6f + 0.4f * pulse), 0.5f, 3.0f);
    }
}

int main (int argc, char** argv)
{
    App app;
    bool demo = false, fullscreen = false, noVsync = false;
    int level = 0;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto value = [&] { return i + 1 < argc ? std::string (argv[++i]) : std::string(); };
        if (a == "--demo") demo = true;
        else if (a == "--fullscreen") fullscreen = true;
        else if (a == "--level") level = std::clamp (std::atoi (value().c_str()) - 1, 0, 2);
        else if (a == "--difficulty") { const auto v = value(); app.difficulty = v == "easy" ? 0 : (v == "hard" ? 2 : 1); }
        else if (a == "--seconds") app.quitAfter = std::atof (value().c_str());
        else if (a == "--shots") app.shotsDir = value();
        else if (a == "--every") app.shotEvery = std::atof (value().c_str());
        else if (a == "--wav") app.wavPath = value();
        else if (a == "--log") app.logPath = value();
        else if (a == "--ldr") app.neon.forceLdr = true;
        else if (a == "--nobloom") app.neon.skipBloom = true;
        else if (a == "--novsync") noVsync = true;
        else if (a == "--reckless") app.pilot.reckless = true;
        else if (a == "--slowtest") app.pilot.slowTest = true;
    }

    // Steam Controller support stays off unless asked for (see sdl3-motion-controllers).
    SDL_SetHint ("SDL_JOYSTICK_HIDAPI_STEAM", "1");
    SDL_SetHint (SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256");

    // OpenGL first: on this laptop it drew the same frame five times faster
    // than Direct3D 11 (2.6 against 14 ms, 2026-10-07). SDL_RENDER_DRIVER in
    // the environment still overrides it.
    SDL_SetHint (SDL_HINT_RENDER_DRIVER, "opengl,direct3d11,direct3d12,vulkan");

    if (! SDL_Init (SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
    {
        SDL_ShowSimpleMessageBox (SDL_MESSAGEBOX_ERROR, "Rise of the Arpeggiator", SDL_GetError(), nullptr);
        return 1;
    }

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
    app.window = SDL_CreateWindow ("Rise of the Arpeggiator", 1600, 900, flags);
    app.renderer = app.window ? SDL_CreateRenderer (app.window, nullptr) : nullptr;
    if (! app.renderer || ! app.neon.init (app.renderer))
    {
        SDL_ShowSimpleMessageBox (SDL_MESSAGEBOX_ERROR, "Rise of the Arpeggiator", SDL_GetError(), nullptr);
        return 1;
    }
    bool vsyncOn = ! noVsync && SDL_SetRenderVSync (app.renderer, 1);

    double refresh = 60.0;
    if (const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode (SDL_GetDisplayForWindow (app.window)))
        if (mode->refresh_rate > 20.0f) refresh = std::min (240.0, (double) mode->refresh_rate);
    int slowPresents = 0;

    if (! app.logPath.empty()) fopen_s (&app.log, app.logPath.c_str(), "w");
    if (app.log) std::fprintf (app.log, "renderer %s\n", SDL_GetRendererName (app.renderer));
    if (app.log) std::fprintf (app.log, "refresh %.0f Hz, vsync %s\n", refresh, vsyncOn ? "on" : "off");

    // The band, and the stream it plays into.
    app.band = std::make_unique<audio::Conductor>();
    app.band->prepare (kSampleRate);
    app.left.resize (1024); app.right.resize (1024); app.interleaved.resize (2048);
    if (! app.wavPath.empty()) { app.recording.reserve ((size_t) kSampleRate * 2 * 200); app.recordOn = true; }

    const SDL_AudioSpec spec { SDL_AUDIO_F32, 2, kSampleRate };
    app.stream = SDL_OpenAudioDeviceStream (SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audioCallback, &app);
    if (app.stream) SDL_ResumeAudioStreamDevice (app.stream);
    if (app.stream && app.log)
    {
        SDL_AudioSpec dev {}; int devFrames = 0;
        SDL_GetAudioDeviceFormat (SDL_GetAudioStreamDevice (app.stream), &dev, &devFrames);
        std::fprintf (app.log, "audio device %d Hz, %d frames per buffer (%.1f ms)\n", dev.freq, devFrames, 1000.0 * devFrames / std::max (1, dev.freq));
    }
    else if (app.log) std::fprintf (app.log, "no audio: %s\n", SDL_GetError());

    // An open pad at start, if one is already plugged in.
    {
        int count = 0;
        if (SDL_JoystickID* ids = SDL_GetGamepads (&count))
        {
            if (count > 0)
            {
                SDL_Event e {};
                e.type = SDL_EVENT_GAMEPAD_ADDED;
                e.gdevice.which = ids[0];
                app.input.handleEvent (e, app.renderer);
            }
            SDL_free (ids);
        }
    }

    loadBest (app);
    app.ambient = std::make_unique<World> (*app.band);
    if (demo) startGame (app, level, true);

    Uint64 last = SDL_GetPerformanceCounter();
    const double freq = (double) SDL_GetPerformanceFrequency();
    const Uint64 started = SDL_GetPerformanceCounter();
    double clock = 0.0, frameSum = 0.0, frameMax = 0.0, nextBeat = 1.0;
    double prof[4] {}; int profN = 0;
    int frames = 0;
    bool running = true;

    while (running)
    {
        app.input.beginFrame();

        SDL_Event e;
        while (SDL_PollEvent (&e))
        {
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F11)
                SDL_SetWindowFullscreen (app.window, ! (SDL_GetWindowFlags (app.window) & SDL_WINDOW_FULLSCREEN));
            app.input.handleEvent (e, app.renderer);
        }

        const Uint64 now = SDL_GetPerformanceCounter();
        const double rawDt = (double) (now - last) / freq;
        last = now;
        const float dt = (float) std::min (rawDt, 1.0 / 20.0);
        clock += dt;
        if (clock > 1.0) { frameSum += rawDt; frameMax = std::max (frameMax, rawDt); ++frames; }
        const double wall = (double) (now - started) / freq;

        if (app.log && wall >= nextBeat)
        {
            nextBeat += 1.0;
            std::fprintf (app.log, "%7.2f wall %7.2f game %6.1f ms  screen %d phase %d layer %d beat %.1f score %d peak %.2f\n",
                          wall, clock, rawDt * 1000.0, (int) app.screen, app.world ? (int) app.world->phase() : -1,
                          app.band->pubLayer.load(), app.band->beatPosition(), app.world ? app.world->score() : 0,
                          app.band->pubPeak.load());
            if (profN) std::fprintf (app.log, "         update %.1f  batch %.1f  bloom %.1f  present %.1f ms\n",
                                     prof[0] / profN * 1e3, prof[1] / profN * 1e3, prof[2] / profN * 1e3, prof[3] / profN * 1e3);
            std::fprintf (app.log, "         quads %d  area %.0f px (%.1f screens)  biggest %.0f px\n", app.neon.statQuads,
                          app.neon.statArea, app.neon.statArea / (1600.0 * 900.0), app.neon.statMaxArea);
            prof[0] = prof[1] = prof[2] = prof[3] = 0.0; profN = 0;
            std::fflush (app.log);
        }

        const auto& keys = app.input.menu();

        const Uint64 tUpdate = SDL_GetPerformanceCounter();
        switch (app.screen)
        {
            case App::Screen::menu:
            {
                app.idle += dt;
                const int before = app.selected;
                if (keys.left)  app.selected = (app.selected + 2) % 3;
                if (keys.right) app.selected = (app.selected + 1) % 3;
                if (keys.up   && app.difficulty > 0) { --app.difficulty; app.idle = 0.0f; }
                if (keys.down && app.difficulty < 2) { ++app.difficulty; app.idle = 0.0f; }
                if (app.selected != before)
                {
                    app.idle = 0.0f;
                    const auto& s = audio::song (app.selected);
                    app.band->post ({ audio::Command::blip, audio::placeAbove (s.keyRoot, 64), 0, 0.5f });
                }
                if (keys.confirm) startGame (app, app.selected, false);
                else if (app.idle > 30.0f) startGame (app, app.selected, true);   // attract mode
                if (keys.back && keys.pause) running = false;
                app.ambient->updateAmbient (dt);
                break;
            }

            case App::Screen::game:
            {
                auto& w = *app.world;

                // In attract mode any key hands the game back to the menu.
                if (app.autopilot && ! demo && (keys.confirm || keys.back || keys.pause || keys.left || keys.right))
                { backToMenu (app); break; }

                if (keys.pause && ! app.autopilot) { app.screen = App::Screen::paused; app.band->post ({ audio::Command::pause }); break; }

                audio::Event ev;
                while (app.band->poll (ev))
                {
                    w.onEvent (ev);
                    if (app.log && ev.type != audio::Event::shot)
                        std::fprintf (app.log, "%8.3f event %d a=%d\n", clock, (int) ev.type, ev.a);
                }

                Controls c;
                if (app.autopilot) c = app.pilot.think (w, dt);
                else c = app.input.controls (w.shipPos());

                const auto phaseBefore = w.phase();

                // Substeps keep fast bullets from passing through small enemies.
                const int steps = std::max (1, (int) std::ceil (dt / (1.0f / 120.0f)));
                for (int s = 0; s < steps; ++s)
                {
                    Controls step = c;
                    if (s > 0) step.bomb = false;
                    w.update (dt / (float) steps, step);
                }

                if (phaseBefore == World::Phase::playing && w.phase() != World::Phase::playing)
                    app.input.rumble (1.0f, 0.6f, 500);
                if (c.bomb) app.input.rumble (0.8f, 0.8f, 300);

                if ((w.phase() == World::Phase::gameOver || w.phase() == World::Phase::cleared))
                {
                    if (keys.confirm && ! app.autopilot) backToMenu (app);
                    else if (app.autopilot && ! demo) backToMenu (app);
                    else if (demo && w.phase() == World::Phase::gameOver) startGame (app, w.levelIndex(), true);
                }
                break;
            }

            case App::Screen::paused:
                if (keys.pause || keys.confirm) { app.screen = App::Screen::game; app.band->post ({ audio::Command::resume }); }
                else if (keys.back) backToMenu (app);
                break;
        }

        // Draw ------------------------------------------------------------------
        const Uint64 tDraw = SDL_GetPerformanceCounter();
        const Vec2 shake = app.world && app.screen != App::Screen::menu ? app.world->shake() : Vec2 {};
        app.neon.begin (shake);

        if (app.screen == App::Screen::menu) drawMenu (app, (float) clock);
        else if (app.world)
        {
            app.world->draw (app.neon);
            if (app.screen == App::Screen::paused)
            {
                app.neon.text ("PAUSED", { kWorldW * 0.5f, kWorldH * 0.42f }, 90.0f, Color { 1, 1, 1 }, 0.5f, 6.0f);
                app.neon.text ("A  RESUME      B  MENU", { kWorldW * 0.5f, kWorldH * 0.42f + 130.0f }, 26.0f,
                               Color { 1, 1, 1, 0.7f }, 0.5f, 2.6f);
            }
            if (app.autopilot && ! demo)
                app.neon.text ("ATTRACT MODE  PRESS A", { kWorldW * 0.5f, kWorldH - 70.0f }, 22.0f,
                               Color { 1, 1, 1, 0.5f + 0.5f * std::sin ((float) clock * 3.0f) }, 0.5f, 2.2f);
        }

        const float flash = app.world && app.screen != App::Screen::menu ? app.world->flash() : 0.0f;
        const Uint64 tEnd = SDL_GetPerformanceCounter();
        // Slow motion's look: trails and chromatic aberration, eased with it.
        const float slowLook = app.world && app.screen != App::Screen::menu ? app.world->slowAmount() : 0.0f;
        // The trail keeps 85% per sixtieth of a second, whatever the frame rate.
        app.neon.trails = slowLook * std::pow (0.85f, (float) rawDt * 60.0f);
        app.neon.aberration = slowLook;
        app.neon.radialBlur = slowLook;
        app.neon.end (1.0f + flash);
        const Uint64 tPresent = SDL_GetPerformanceCounter();

        if (! app.shotsDir.empty() && wall >= app.nextShot)
        {
            screenshot (app);
            app.nextShot += app.shotEvery;
        }

        SDL_RenderPresent (app.renderer);
        const Uint64 tDone = SDL_GetPerformanceCounter();

        // Vsync that waits several refreshes per frame is being throttled —
        // Windows does this to a window it thinks is hidden, and it made the
        // first test runs crawl at 3 fps. Then pace the frames ourselves.
        const double presentTime = (double) (tDone - tPresent) / freq;
        if (vsyncOn)
        {
            slowPresents = presentTime > 3.0 / refresh ? slowPresents + 1 : 0;
            if (slowPresents >= 20)
            {
                SDL_SetRenderVSync (app.renderer, 0);
                vsyncOn = false;
                if (app.log) std::fprintf (app.log, "vsync throttled, pacing at %.0f Hz instead\n", refresh);
            }
        }
        else
        {
            const double spent = (double) (SDL_GetPerformanceCounter() - now) / freq;
            const double wait = 1.0 / refresh - spent;
            if (wait > 0.0005) SDL_DelayPrecise ((Uint64) (wait * 1.0e9));
        }
        prof[0] += (double) (tDraw - tUpdate) / freq; prof[1] += (double) (tEnd - tDraw) / freq;
        prof[2] += (double) (tPresent - tEnd) / freq; prof[3] += (double) (tDone - tPresent) / freq; ++profN;

        // Mouse aim needs world coordinates; Neon knows the transform after begin().
        {
            const Vec2 px = app.input.mouseWindowPixels();
            int w = 1, h = 1;
            SDL_GetCurrentRenderOutputSize (app.renderer, &w, &h);
            const float sc = std::min ((float) w / kWorldW, (float) h / kWorldH);
            const Vec2 off { ((float) w - kWorldW * sc) * 0.5f, ((float) h - kWorldH * sc) * 0.5f };
            app.input.setMouseWorld (app.neon.screenToWorld (px));   // through the zoom, when there is one
        }

        if (app.quitAfter > 0.0 && wall >= app.quitAfter) running = false;
    }

    if (app.stream) SDL_DestroyAudioStream (app.stream);
    app.recordOn = false;

    if (app.log)
    {
        std::fprintf (app.log, "audio callbacks %d, request %d-%d frames\n", app.callbacks.load(), app.minRequest.load(), app.maxRequest.load());
        std::fprintf (app.log, "frames %d  mean %.2f ms  worst %.2f ms  peak %.2f\n", frames,
                      frames ? frameSum / frames * 1000.0 : 0.0, frameMax * 1000.0, app.band->pubPeak.load());
        if (app.world) std::fprintf (app.log, "score %d\n", app.world->score());
        std::fclose (app.log);
    }
    if (! app.wavPath.empty()) writeWav (app.wavPath, app.recording);

    app.input.shutdown();
    app.neon.shutdown();
    SDL_DestroyRenderer (app.renderer);
    SDL_DestroyWindow (app.window);
    SDL_Quit();
    return 0;
}
