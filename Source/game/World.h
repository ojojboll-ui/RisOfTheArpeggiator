#pragma once

#include <string>
#include <vector>

#include "../audio/Conductor.h"
#include "Grid.h"
#include "Math.h"
#include "Neon.h"

/**
    The field, everything on it, and the rules.

    The world never decides *when* anything musical happens. It reads the band
    (bar, beat, layer, chord) and reacts: a Shot event from the arpeggio is
    when a bullet leaves the ship, a Bar event is when a wave arrives, the end
    of a death's drop is when the ship comes back. What it sends the other way
    is intent — which chord the left stick points at, whether the right one is
    out, and what died where.
*/
namespace np
{
    /** The playfield inside the window's 1920 × 1080. */
    constexpr float kFieldL = 30.0f, kFieldT = 84.0f, kFieldR = 1890.0f, kFieldB = 1050.0f;

    constexpr float kShipSpeed   = 560.0f;
    constexpr float kBulletSpeed = 1500.0f;

    struct Controls
    {
        Vec2 move;              // -1..1, deadzone already taken out, +y down
        Vec2 aim;               // direction and how far out
        bool fire = false;
        bool bomb = false;      // pressed this frame
        bool slow = false;      // slow motion, held
    };

    enum class EnemyKind { wanderer, seeker, dodger, splitter, shard, well };

    /** Three difficulties (2026-10-09). Normal is the game as it was tuned.
        `spawn` scales every wave's size, `speed` every enemy's, and the
        half-bar waves start at `halfBarLayer`. */
    struct Difficulty { const char* name; float spawn, speed; int lives, bombs, halfBarLayer; };

    inline const Difficulty& difficulty (int index)
    {
        static const Difficulty d[3] {
            { "EASY",   0.60f, 0.85f, 5, 4, 4 },
            { "NORMAL", 1.00f, 1.00f, 3, 3, 2 },
            { "HARD",   1.45f, 1.15f, 3, 2, 1 } };
        return d[index < 0 ? 0 : (index > 2 ? 2 : index)];
    }

    class World
    {
    public:
        enum class Phase { playing, dead, gameOver, cleared };

        explicit World (audio::Conductor& bandIn) : band (bandIn)
        {
            grid.build (kFieldL, kFieldT, kFieldR, kFieldB, 40.0f);
        }

        void start (int level, int difficultyIndex = 1);
        int difficultyIndex() const { return diff; }
        void update (float dt, const Controls& in);
        void onEvent (const audio::Event& e);
        void draw (Neon& neon);

        /** Grid and particles only — for the menu's background. */
        void updateAmbient (float dt);
        void drawAmbient (Neon& neon, Color tint);

        Phase phase() const { return state; }
        int score() const { return points; }
        int levelIndex() const { return level; }
        Vec2 shake() const { return shakeVec; }
        float flash() const { return flashAmount; }

        /** For the autopilot: what is on the field. */
        struct Threat { Vec2 pos, vel; float radius; bool live; };
        void threats (std::vector<Threat>& out) const;
        Vec2 shipPos() const { return ship.pos; }
        bool shipAlive() const { return ship.alive; }

        /** 0 at full speed, 1 at full slow motion, eased like the clock. */
        float slowAmount() const { return std::clamp ((1.0f - timeScale) / (1.0f - kSlowSpeed), 0.0f, 1.0f); }
        int bombsLeft() const { return bombs; }

    private:
        struct Ship
        {
            Vec2 pos, vel;
            float angle = -kPi * 0.5f;
            float invulnerable = 0.0f;
            bool alive = true;
        };

        struct Enemy
        {
            EnemyKind kind = EnemyKind::seeker;
            Vec2 pos, vel;
            float angle = 0.0f, spin = 0.0f, radius = 20.0f;
            float spawn = 0.0f;         // seconds left of spawning in, harmless meanwhile
            float hp = 1.0f, wander = 0.0f, flash = 0.0f, eaten = 0.0f;
            int value = 50;
            bool alive = true;
        };

        struct Bullet { Vec2 pos, vel; int chord = 0, note = -1; float life = 1.5f; bool accent = false; bool alive = true; };

        struct Particle
        {
            Vec2 pos, vel;
            Color color;
            float life = 1.0f, maxLife = 1.0f, width = 2.4f, drag = 0.96f;
        };

        struct Pickup { Vec2 pos, vel; float life = 9.0f, angle = 0.0f; bool alive = true; };
        struct Popup { Vec2 pos; std::string text; float life = 1.0f; Color color; float size = 22.0f; };
        struct Ring  { Vec2 pos; float radius = 0.0f, speed = 1500.0f, life = 1.0f; Color color; };

        // Rules ------------------------------------------------------------------
        void updateShip (float dt, const Controls& in);
        void updateChordRequest (Vec2 move);
        void updateEnemies (float dt);
        void updateBullets (float dt);
        void updateParticles (float dt);
        void updatePickups (float dt);
        void collide();

        void spawnShot (int note, int chord, int index, bool accent);
        void spawnWave (int bar, bool half);
        void spawnEnemy (EnemyKind kind, Vec2 pos, float delay = 0.0f);
        Vec2 farPoint (float minDist);
        void kill (Enemy& e, int chord, bool scored, bool sounded, int note = -1);
        void explode (Vec2 pos, Color a, Color b, int count, float speed, float life);
        void shipDies();
        void respawn();
        void detonateBomb();
        void addScore (int amount, Vec2 where);

        // Drawing -------------------------------------------------------------------
        void drawEnemy (Neon& neon, const Enemy& e) const;
        void drawShip (Neon& neon) const;
        void drawHud (Neon& neon) const;
        void drawWheel (Neon& neon, Vec2 centre) const;

        static Color chordColor (int slot);
        Color enemyColor (EnemyKind k) const;

        audio::Conductor& band;
        Rng rng;
        Grid grid;

        Phase state = Phase::playing;
        int level = 0, diff = 1;
        Ship ship;
        std::vector<Enemy> enemies;
        std::vector<Enemy> incoming;    // born this frame; joined after the loops that iterate enemies
        std::vector<Bullet> bullets;
        std::vector<Particle> particles;
        std::vector<Pickup> pickups;
        std::vector<Popup> popups;
        std::vector<Ring> rings;

        int points = 0, lives = 3, bombs = 3, multiplier = 1, killsSinceDeath = 0;
        static constexpr int kLifeEvery = 150000, kBombEvery = 120000;
        int nextLifeAt = kLifeEvery, nextBombAt = kBombEvery;
        Vec2 aimDir { 0.0f, -1.0f };
        int wheelSlot = 0;              // what the stick points at, with hysteresis
        float deadTime = 0.0f, time = 0.0f, clearedTime = 0.0f;
        int lastKicks = 0, lastHalfBars = 0;

        // Slow motion: a meter that drains while it is held and refills with
        // kills; while it runs everything but the ship and its shots moves at
        // kSlowSpeed (2026-10-10).
        static constexpr float kSlowSpeed = 0.3f, kSlowDrain = 1.0f / 5.0f, kSlowPerKill = 0.04f;
        static constexpr float kSlowShipSpeed = kSlowSpeed;   // 0.55 for a day; now as slow as the rest, it is faster to begin with
        float slowMeter = 1.0f, timeScale = 1.0f;
        bool slowActive = false;
        float slowStartMeter = 1.0f, slowProgress = 0.0f, sentSlowDepth = 0.0f;   // how far through this slow motion's meter
        static constexpr float kSlowDip = 0.3f;   // where the slow filter lands at once: ~6 kHz
        void setSlow (bool on);
        float kickPulse = 0.0f, chordFlash = 0.0f, flashAmount = 0.0f;
        float shakeAmount = 0.0f;
        Vec2 shakeVec;
        std::string banner; float bannerLife = 0.0f; Color bannerColor;
    };


    /** Power-ups are green and bombs red, so the two never look alike. */
    constexpr Color kPowerUpColor { 0.35f, 1.0f, 0.45f };
    constexpr Color kBombColor    { 1.0f, 0.22f, 0.18f };

    /** Per-level looks. */
    Color levelTint (int level);
    Color levelAccent (int level);
}
