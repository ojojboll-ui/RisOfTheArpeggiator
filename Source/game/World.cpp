#include "World.h"

#include <cstdio>

namespace np
{
    using audio::Command;
    using audio::Event;

    //==========================================================================
    // Looks

    Color levelTint (int level)
    {
        static const Color tints[3] { { 0.22f, 0.36f, 1.0f }, { 0.50f, 1.0f, 0.18f }, { 0.85f, 0.25f, 1.0f } };
        return tints[std::clamp (level, 0, 2)];
    }

    Color levelAccent (int level)
    {
        static const Color accents[3] { { 0.35f, 0.85f, 1.0f }, { 1.0f, 0.95f, 0.25f }, { 1.0f, 0.45f, 0.85f } };
        return accents[std::clamp (level, 0, 2)];
    }

    /** One colour per wheel slot, so the harmony can be seen: the ship's
        exhaust, the bullets and the bursts all wear the chord they belong to. */
    Color World::chordColor (int slot)
    {
        static const Color c[4] { { 0.25f, 0.92f, 1.0f },     // i    up
                                  { 1.0f, 0.30f, 0.85f },     // VI   right
                                  { 1.0f, 0.72f, 0.18f },     // III  down
                                  { 0.42f, 1.0f, 0.40f } };   // VII  left
        return c[slot & 3];
    }

    Color World::enemyColor (EnemyKind k) const
    {
        switch (k)
        {
            case EnemyKind::wanderer: return { 0.72f, 0.38f, 1.0f };
            case EnemyKind::seeker:   return { 0.25f, 0.82f, 1.0f };
            case EnemyKind::dodger:   return { 0.30f, 1.0f, 0.42f };
            case EnemyKind::splitter: return { 1.0f, 0.30f, 0.80f };
            case EnemyKind::shard:    return { 1.0f, 0.55f, 0.85f };
            case EnemyKind::well:     return { 1.0f, 0.36f, 0.14f };
        }
        return {};
    }

    namespace
    {
        // How big a chord each kind lets out: 0 bright and short, 2 dark and long.
        int chordSize (EnemyKind k)
        {
            switch (k)
            {
                case EnemyKind::wanderer: case EnemyKind::shard: return 0;
                case EnemyKind::well:                            return 2;
                default:                                         return 1;
            }
        }

        Vec2 clampToField (Vec2 p, float margin)
        {
            return { std::clamp (p.x, kFieldL + margin, kFieldR - margin),
                     std::clamp (p.y, kFieldT + margin, kFieldB - margin) };
        }

        const Vec2 kCentre { (kFieldL + kFieldR) * 0.5f, (kFieldT + kFieldB) * 0.5f };
    }

    //==========================================================================

    void World::start (int levelIn, int difficultyIn)
    {
        diff = std::clamp (difficultyIn, 0, 2);
        level = std::clamp (levelIn, 0, 2);
        state = Phase::playing;

        grid.build (kFieldL, kFieldT, kFieldR, kFieldB, 40.0f);

        ship = {};
        ship.pos = kCentre;
        ship.invulnerable = 1.5f;

        enemies.clear(); incoming.clear(); bullets.clear(); particles.clear();
        pickups.clear(); popups.clear(); rings.clear();
        enemies.reserve (256); bullets.reserve (512); particles.reserve (8192);

        points = 0; lives = difficulty (diff).lives; bombs = difficulty (diff).bombs; multiplier = 1; killsSinceDeath = 0;
        nextLifeAt = kLifeEvery; nextBombAt = kBombEvery;
        wheelSlot = audio::song (level).homeSlot;
        deadTime = time = clearedTime = 0.0f;
        lastKicks = band.pubKicks.load();
        lastHalfBars = 0;
        slowMeter = 1.0f; timeScale = 1.0f; slowActive = false; slowProgress = 0.0f;
        kickPulse = chordFlash = flashAmount = shakeAmount = 0.0f;
        banner = audio::song (level).name; bannerLife = 2.5f; bannerColor = levelAccent (level);

        band.requestedChord = wheelSlot;   // the song starts on its tonic
        band.firing = false;
        // A fresh arpeggio direction at every start, for songs that draw one.
        rng.s = (std::uint32_t) (SDL_GetPerformanceCounter() | 1u);
        band.post ({ Command::start, level, (int) (rng.next() % 4u) });
    }

    //==========================================================================
    // Events from the band

    void World::onEvent (const Event& e)
    {
        switch (e.type)
        {
            case Event::shot:
                if (state == Phase::playing && ship.alive)
                    spawnShot (e.a, e.b, e.c, e.d != 0);
                break;

            case Event::bar:
                if (state == Phase::playing) spawnWave (e.a, false);
                break;

            case Event::chord:
                chordFlash = 1.0f;
                break;

            case Event::layerUp:
                banner = std::string ("+ ") + audio::layerName (audio::song (level), e.a);
                bannerLife = 2.2f; bannerColor = levelAccent (level);
                flashAmount = std::max (flashAmount, 0.35f);
                grid.push (ship.pos, 900.0f, 700.0f);
                break;

            case Event::layerDown:
                banner = std::string ("- ") + audio::layerName (audio::song (level), e.a + 1);
                bannerLife = 2.2f; bannerColor = { 1.0f, 0.25f, 0.2f };
                break;

            case Event::dropEnd:
                if (state == Phase::dead) respawn();
                break;

            case Event::songEnd:
                if (state == Phase::playing || state == Phase::dead)
                {
                    state = Phase::cleared;
                    clearedTime = 0.0f;
                    for (auto& en : enemies) if (en.alive) kill (en, 0, false, false);
                    const int bonus = lives * 5000 + bombs * 2000;
                    points += bonus;
                    banner = "LEVEL CLEAR"; bannerLife = 1.0e9f; bannerColor = levelAccent (level);
                    for (int i = 0; i < 6; ++i)
                        explode (kCentre + rng.inDisc (500.0f), chordColor (i), levelAccent (level), 140, 900.0f, 2.0f);
                }
                break;
        }
    }

    //==========================================================================

    void World::update (float dt, const Controls& in)
    {
        time += dt;

        // The kick, as the band counts them.
        const int kicks = band.pubKicks.load();
        if (kicks != lastKicks)
        {
            lastKicks = kicks;
            kickPulse = 1.0f;
            if (ship.alive) grid.push (ship.pos, 160.0f, 260.0f);
            for (const auto& e : enemies)
                if (e.alive && e.kind == EnemyKind::well && e.spawn <= 0.0f) grid.pull (e.pos, 220.0f, 260.0f);
        }

        // From the third layer on, a second, smaller wave lands on every half bar.
        const int halfBars = (int) (band.beatPosition() / 2.0);
        if (halfBars != lastHalfBars)
        {
            lastHalfBars = halfBars;
            if (halfBars % 2 == 1 && band.pubLayer.load() >= difficulty (diff).halfBarLayer && state == Phase::playing)
                spawnWave (halfBars / 2, true);
        }

        kickPulse *= std::exp (-dt * 7.0f);
        chordFlash *= std::exp (-dt * 5.0f);
        flashAmount *= std::exp (-dt * 4.0f);
        bannerLife -= dt;

        shakeAmount *= std::exp (-dt * 9.0f);
        shakeVec = shakeAmount > 0.3f ? rng.inDisc (shakeAmount) : Vec2 {};

        if (state == Phase::playing)
        {
            // The ship slows with the world, but less than the enemies do; its
            // shots keep their speed (2026-10-10).
            const float slowAmount = std::clamp ((1.0f - timeScale) / (1.0f - kSlowSpeed), 0.0f, 1.0f);
            updateShip (dt * (1.0f - (1.0f - kSlowShipSpeed) * slowAmount), in);

            if (in.bomb) detonateBomb();

            // Slow motion: needs a little in the meter to start, runs it dry.
            const bool want = in.slow && ship.alive && (slowActive ? slowMeter > 0.0f : slowMeter > 0.15f);
            setSlow (want);
            if (slowActive)
            {
                slowMeter = std::max (0.0f, slowMeter - kSlowDrain * dt);
                slowProgress = std::clamp ((slowStartMeter - slowMeter) / slowStartMeter, 0.0f, 1.0f);
            }
        }
        else
        {
            band.firing = false;
            setSlow (false);
        }

        // The world's clock eases between full speed and slow, so it never jerks.
        timeScale += ((slowActive ? kSlowSpeed : 1.0f) - timeScale) * std::min (1.0f, dt * 12.0f);
        const float realDt = dt;
        dt *= timeScale;

        // The band's slow filter: a noticeable but gentle dip as slow motion
        // starts (30% of the way, about 6 kHz), then down with the zoom on the
        // same logarithmic course, sent whenever it has moved (2026-10-14).
        {
            const float depth = slowAmount() * (kSlowDip + (1.0f - kSlowDip) * slowProgress);
            if (std::abs (depth - sentSlowDepth) > 0.002f || (depth == 0.0f && sentSlowDepth != 0.0f))
            {
                sentSlowDepth = depth;
                band.post ({ Command::slow, depth > 0.0f ? 1 : 0, 0, depth });
            }
        }

        if (state == Phase::dead)
        {
            deadTime += dt;
            // The band brings the ship back when the music returns; this is
            // only for when the music cannot (the song ended mid-drop).
            if (deadTime > 6.0f) respawn();
        }

        if (state == Phase::cleared) clearedTime += dt;

        updateEnemies (dt);
        updateBullets (realDt);   // the shots are the player's, so they keep full speed
        updatePickups (dt);
        collide();
        updateParticles (dt);
        grid.update (dt);

        for (auto& p : popups) { p.life -= dt; p.pos.y -= 40.0f * dt; }
        popups.erase (std::remove_if (popups.begin(), popups.end(), [] (const Popup& p) { return p.life <= 0.0f; }), popups.end());

        for (auto& r : rings) { r.radius += r.speed * dt; r.life -= dt; }
        rings.erase (std::remove_if (rings.begin(), rings.end(), [] (const Ring& r) { return r.life <= 0.0f; }), rings.end());

        enemies.erase (std::remove_if (enemies.begin(), enemies.end(), [] (const Enemy& e) { return ! e.alive; }), enemies.end());
        enemies.insert (enemies.end(), incoming.begin(), incoming.end());
        incoming.clear();
        bullets.erase (std::remove_if (bullets.begin(), bullets.end(), [] (const Bullet& b) { return ! b.alive; }), bullets.end());
        pickups.erase (std::remove_if (pickups.begin(), pickups.end(), [] (const Pickup& p) { return ! p.alive; }), pickups.end());
    }

    void World::setSlow (bool on)
    {
        if (on == slowActive) return;
        slowActive = on;
        if (on) { slowStartMeter = std::max (0.01f, slowMeter); slowProgress = 0.0f; }
    }

    void World::updateAmbient (float dt)
    {
        time += dt;

        const int kicks = band.pubKicks.load();
        if (kicks != lastKicks) { lastKicks = kicks; kickPulse = 1.0f; }
        kickPulse *= std::exp (-dt * 7.0f);

        // A slow drift of disturbances so the menu floor is never still.
        if (rng.chance (dt * 1.4f))
        {
            const Vec2 p = kCentre + Vec2 { rng.range (-800.0f, 800.0f), rng.range (-420.0f, 420.0f) };
            grid.push (p, rng.range (300.0f, 700.0f), 220.0f);
            explode (p, hsv (rng.unit(), 0.7f, 1.0f), hsv (rng.unit(), 0.5f, 1.0f), 40, 500.0f, 1.2f);
        }

        updateParticles (dt);
        grid.update (dt);
    }

    void World::drawAmbient (Neon& neon, Color tint)
    {
        grid.draw (neon, tint, kickPulse);
        for (const auto& p : particles)
        {
            const float a = p.life / p.maxLife;
            const Vec2 tail = p.pos - p.vel * 0.025f;
            neon.line (tail, p.pos, p.color.withAlpha (a * p.color.a), p.width);
        }
    }

    //==========================================================================
    // Ship

    void World::updateChordRequest (Vec2 move)
    {
        // The wheel reads the stick only when it is clearly pushed, and keeps
        // the current chord until the stick is well inside a neighbour's
        // quarter — 12 degrees past the boundary. Dodging back and forth over
        // a diagonal does not flip the harmony on every beat.
        if (move.length() < 0.5f) return;

        const float angle = std::atan2 (move.x, -move.y);     // 0 up, clockwise
        const float centre = (float) wheelSlot * kPi * 0.5f;
        const float hysteresis = (45.0f + 12.0f) * kPi / 180.0f;

        if (std::abs (angleDiff (angle, centre)) > hysteresis)
        {
            float a = std::fmod (angle + kTau + kPi * 0.25f, kTau);
            wheelSlot = (int) (a / (kPi * 0.5f)) & 3;
        }

        band.requestedChord = wheelSlot;
    }

    void World::updateShip (float dt, const Controls& in)
    {
        if (! ship.alive) { band.firing = false; return; }

        // Speed follows deflection with a slight curve, so small corrections
        // are fine; this is a position control, not something heard.
        Vec2 move = in.move;
        const float m = std::min (1.0f, move.length());
        if (m > 0.0f) move = move.normalised() * std::pow (m, 1.3f);

        const Vec2 target = move * kShipSpeed;
        ship.vel += (target - ship.vel) * std::min (1.0f, dt * 14.0f);
        ship.pos += ship.vel * dt;

        // Wells pull the ship as well.
        for (const auto& e : enemies)
        {
            if (! e.alive || e.kind != EnemyKind::well || e.spawn > 0.0f) continue;
            const Vec2 d = e.pos - ship.pos;
            const float dist = d.length();
            if (dist < 420.0f && dist > 1.0f) ship.pos += d / dist * (120.0f * (1.0f - dist / 420.0f) * dt);
        }

        ship.pos = clampToField (ship.pos, 18.0f);

        if (ship.vel.length() > 40.0f)
        {
            const float want = ship.vel.angle();
            ship.angle += angleDiff (want, ship.angle) * std::min (1.0f, dt * 16.0f);

            // Exhaust in the colour of the chord being played.
            const int n = (int) (ship.vel.length() / kShipSpeed * 3.0f) + 1;
            const Color c = chordColor (band.pubChord.load());
            for (int i = 0; i < n; ++i)
            {
                Particle p;
                p.pos = ship.pos - fromAngle (ship.angle, 14.0f);
                p.vel = -ship.vel.normalised() * rng.range (150.0f, 380.0f) + rng.inDisc (90.0f);
                p.color = c.withAlpha (0.7f);
                p.life = p.maxLife = rng.range (0.25f, 0.55f);
                p.width = 2.2f;
                p.drag = 0.9f;
                particles.push_back (p);
            }
        }

        if (ship.invulnerable > 0.0f) ship.invulnerable -= dt;

        updateChordRequest (in.move);

        const bool firing = in.fire && in.aim.length() > 0.2f;
        band.firing = firing;
        band.aimPan = std::clamp ((ship.pos.x - kFieldL) / (kFieldR - kFieldL), 0.0f, 1.0f);
        if (firing) aimDir = in.aim.normalised();
    }

    void World::spawnShot (int note, int chord, int index, bool accent)
    {
        const int rate = band.pubArpRate.load();
        const float base = aimDir.angle();

        // The arpeggio fans the shots a little: each step of the cycle leaves
        // at its own angle, so a run up the chord is a sweep across the aim.
        const float fan = ((float) index / 100.0f - 0.5f) * 0.084f;   // index is the place in the arp cycle, 0..100
        const Color c = chordColor (chord);

        auto fire = [&] (float angle, Vec2 offset)
        {
            Bullet b;
            b.pos = ship.pos + fromAngle (base, 20.0f) + offset;
            b.vel = fromAngle (angle, kBulletSpeed);
            b.chord = chord;
            b.note = note;
            b.accent = accent;
            bullets.push_back (b);
        };

        if (rate == 0)
        {
            for (int k = -1; k <= 1; ++k) fire (base + fan + 0.10f * (float) k, {});
        }
        else
        {
            const Vec2 side = fromAngle (base + kPi * 0.5f, 8.0f);
            fire (base + fan, side);
            fire (base + fan, -side);
            if (rate == 2) fire (base + fan, {});
        }

        Particle flashP;
        flashP.pos = ship.pos + fromAngle (base, 26.0f);
        flashP.vel = fromAngle (base, 300.0f);
        flashP.color = c.withAlpha (accent ? 1.0f : 0.6f);
        flashP.life = flashP.maxLife = 0.08f;
        flashP.width = accent ? 9.0f : 6.0f;
        particles.push_back (flashP);
    }

    //==========================================================================
    // Enemies

    void World::spawnEnemy (EnemyKind kind, Vec2 pos, float delay)
    {
        if (enemies.size() + incoming.size() >= 220) return;

        Enemy e;
        e.kind = kind;
        e.pos = clampToField (pos, 30.0f);
        e.spawn = 0.7f + delay;
        e.angle = rng.range (0.0f, kTau);
        e.wander = rng.range (0.0f, kTau);

        switch (kind)
        {
            case EnemyKind::wanderer: e.radius = 20.0f; e.value = 25;  e.spin = 3.0f; break;
            case EnemyKind::seeker:   e.radius = 18.0f; e.value = 50;  break;
            case EnemyKind::dodger:   e.radius = 19.0f; e.value = 100; e.spin = 1.2f; break;
            case EnemyKind::splitter: e.radius = 22.0f; e.value = 75;  e.spin = 1.5f; break;
            case EnemyKind::shard:    e.radius = 10.0f; e.value = 50;  e.spin = 6.0f; e.spawn = delay; break;
            case EnemyKind::well:     e.radius = 26.0f; e.value = 300; e.hp = 14.0f; break;
        }

        incoming.push_back (e);
    }

    Vec2 World::farPoint (float minDist)
    {
        for (int tries = 0; tries < 30; ++tries)
        {
            const Vec2 p { rng.range (kFieldL + 60.0f, kFieldR - 60.0f), rng.range (kFieldT + 60.0f, kFieldB - 60.0f) };
            if ((p - ship.pos).length() >= minDist) return p;
        }
        return { ship.pos.x < kCentre.x ? kFieldR - 80.0f : kFieldL + 80.0f, kCentre.y };
    }

    void World::spawnWave (int bar, bool half)
    {
        // The first bar is a breath: kick and bass, nothing to shoot.
        if (bar < 1 || ! ship.alive) return;

        const int layer = band.pubLayer.load();
        const float intensity = (0.5f * (float) level + 0.35f * (float) layer + (float) bar / 28.0f) * difficulty (diff).spawn;
        const int full = std::min ((int) (18.0f * difficulty (diff).spawn), std::max (2, (int) ((3.0f + intensity * 2.2f) * std::sqrt (difficulty (diff).spawn))));
        const int budget = half ? std::max (2, full / 2) : full;

        int wells = 0;
        for (const auto& e : enemies) if (e.alive && e.kind == EnemyKind::well) ++wells;

        // What this level's floor can throw. Each level adds a kind.
        struct Option { int kind; float weight; };
        std::vector<Option> options {
            { 0, 1.0f },                                  // wanderers along an edge
            { 1, 1.0f },                                  // seekers from the corners
            { 2, level >= 0 ? 0.7f : 0.0f },              // dodgers in a cluster
            { 3, level >= 1 ? 0.6f : 0.0f },              // splitters
            { 4, ! half && (level >= 2 || (level == 1 && layer >= 3)) && wells < 2 ? 0.25f : 0.0f },   // a well
            { 5, ! half && intensity > 2.5f && bar % 8 == 4 ? 2.0f : 0.0f }                             // a ring round the ship
        };

        float total = 0.0f;
        for (const auto& o : options) total += o.weight;
        float pick = rng.range (0.0f, total);
        int choice = 0;
        for (const auto& o : options) { if ((pick -= o.weight) <= 0.0f) { choice = o.kind; break; } }

        switch (choice)
        {
            case 0:
            {
                const int edge = rng.below (4);
                for (int i = 0; i < budget; ++i)
                {
                    const float t = ((float) i + 0.5f) / (float) budget;
                    Vec2 p;
                    if (edge == 0) p = { lerp (kFieldL, kFieldR, t), kFieldT + 40.0f };
                    if (edge == 1) p = { kFieldR - 40.0f, lerp (kFieldT, kFieldB, t) };
                    if (edge == 2) p = { lerp (kFieldL, kFieldR, t), kFieldB - 40.0f };
                    if (edge == 3) p = { kFieldL + 40.0f, lerp (kFieldT, kFieldB, t) };
                    spawnEnemy (EnemyKind::wanderer, p, 0.04f * (float) i);
                }
                break;
            }

            case 1:
            {
                const Vec2 corners[4] { { kFieldL + 60, kFieldT + 60 }, { kFieldR - 60, kFieldT + 60 },
                                        { kFieldR - 60, kFieldB - 60 }, { kFieldL + 60, kFieldB - 60 } };
                const int per = std::max (1, budget / 4);
                for (const auto& c : corners)
                {
                    if ((c - ship.pos).length() < 300.0f) continue;
                    for (int i = 0; i < per; ++i) spawnEnemy (EnemyKind::seeker, c + rng.inDisc (40.0f), 0.08f * (float) i);
                }
                break;
            }

            case 2:
            {
                const Vec2 c = farPoint (500.0f);
                for (int i = 0; i < std::max (2, budget * 2 / 3); ++i)
                    spawnEnemy (EnemyKind::dodger, c + rng.inDisc (90.0f), 0.05f * (float) i);
                break;
            }

            case 3:
            {
                for (int i = 0; i < std::max (1, budget / 3); ++i)
                    spawnEnemy (EnemyKind::splitter, farPoint (450.0f), 0.1f * (float) i);
                break;
            }

            case 4:
                spawnEnemy (EnemyKind::well, farPoint (550.0f));
                for (int i = 0; i < budget / 2; ++i) spawnEnemy (EnemyKind::wanderer, farPoint (400.0f), 0.1f * (float) i);
                break;

            case 5:
            {
                const int n = std::min (12, budget);
                for (int i = 0; i < n; ++i)
                    spawnEnemy (EnemyKind::seeker, ship.pos + fromAngle (kTau * (float) i / (float) n, 470.0f), 0.3f);
                break;
            }
        }
    }

    void World::updateEnemies (float dt)
    {
        const int layer = band.pubLayer.load();
        const float speedUp = (1.0f + 0.08f * (float) level + 0.03f * (float) layer) * difficulty (diff).speed;
        const Vec2 target = ship.alive ? ship.pos : kCentre;

        for (auto& e : enemies)
        {
            if (! e.alive) continue;

            e.flash = std::max (0.0f, e.flash - dt * 6.0f);
            e.angle += e.spin * dt;

            if (e.spawn > 0.0f) { e.spawn -= dt; continue; }

            const Vec2 toShip = target - e.pos;
            const float dist = std::max (1.0f, toShip.length());

            switch (e.kind)
            {
                case EnemyKind::wanderer:
                {
                    e.wander += rng.range (-2.5f, 2.5f) * dt;
                    const Vec2 want = fromAngle (e.wander, 150.0f * speedUp);
                    e.vel += (want - e.vel) * std::min (1.0f, dt * 2.0f);
                    break;
                }

                case EnemyKind::seeker:
                {
                    const Vec2 want = toShip / dist * (230.0f * speedUp);
                    e.vel += (want - e.vel) * std::min (1.0f, dt * 2.5f);
                    e.angle = e.vel.angle();
                    break;
                }

                case EnemyKind::dodger:
                {
                    Vec2 want = toShip / dist * (210.0f * speedUp);

                    // Sidestep any bullet heading this way.
                    for (const auto& b : bullets)
                    {
                        const Vec2 d = e.pos - b.pos;
                        const float bd = d.length();
                        if (bd < 140.0f && dot (d, b.vel) > 0.0f)
                        {
                            const Vec2 side = b.vel.normalised().perp();
                            want += side * (dot (side, d) > 0.0f ? 520.0f : -520.0f);
                            break;
                        }
                    }
                    e.vel += (want - e.vel) * std::min (1.0f, dt * 6.0f);
                    break;
                }

                case EnemyKind::splitter:
                {
                    const Vec2 want = toShip / dist * (150.0f * speedUp);
                    e.vel += (want - e.vel) * std::min (1.0f, dt * 1.6f);
                    break;
                }

                case EnemyKind::shard:
                {
                    // Circle in on the ship rather than straight at it.
                    const Vec2 want = (toShip / dist + toShip.perp() / dist * 0.8f).normalised() * (300.0f * speedUp);
                    e.vel += (want - e.vel) * std::min (1.0f, dt * 3.0f);
                    break;
                }

                case EnemyKind::well:
                {
                    e.vel *= std::exp (-dt * 1.5f);
                    e.radius = 26.0f + e.eaten * 1.6f + 4.0f * kickPulse;
                    grid.pull (e.pos, 30.0f * dt * 60.0f * 0.25f, 220.0f);

                    // Swallow anything that strays too close.
                    for (auto& o : enemies)
                    {
                        if (&o == &e || ! o.alive || o.kind == EnemyKind::well || o.spawn > 0.0f) continue;
                        const Vec2 d = e.pos - o.pos;
                        const float od = d.length();
                        if (od < 360.0f && od > 1.0f) o.vel += d / od * (900.0f * (1.0f - od / 360.0f) * dt);
                        if (od < e.radius + o.radius * 0.5f)
                        {
                            o.alive = false;
                            e.eaten += 1.0f;
                            e.hp += 1.0f;
                            explode (o.pos, enemyColor (o.kind), enemyColor (EnemyKind::well), 20, 300.0f, 0.6f);
                        }
                    }

                    // Overfed: it bursts and lets out what it ate.
                    if (e.eaten >= 12.0f)
                    {
                        e.alive = false;
                        explode (e.pos, enemyColor (EnemyKind::well), { 1, 1, 1 }, 260, 1100.0f, 1.8f);
                        grid.push (e.pos, 1600.0f, 500.0f);
                        band.post ({ Command::kill, band.pubChord.load(), 2, e.pos.x / kWorldW, -1, (int) EnemyKind::well });
                        for (int i = 0; i < 8; ++i) spawnEnemy (EnemyKind::shard, e.pos + fromAngle (kTau * i / 8.0f, 30.0f), 0.0f);
                        shakeAmount = std::max (shakeAmount, 12.0f);
                    }
                    break;
                }
            }

            e.pos += e.vel * dt;

            // Walls: bounce, and keep inside. The wander heading is only mirrored
            // while it still points into the wall — mirroring it every frame
            // against the wall left the pinwheels shivering there.
            if (e.pos.x < kFieldL + e.radius) { e.pos.x = kFieldL + e.radius; e.vel.x = std::abs (e.vel.x); if (std::cos (e.wander) < 0.0f) e.wander = kPi - e.wander; }
            if (e.pos.x > kFieldR - e.radius) { e.pos.x = kFieldR - e.radius; e.vel.x = -std::abs (e.vel.x); if (std::cos (e.wander) > 0.0f) e.wander = kPi - e.wander; }
            if (e.pos.y < kFieldT + e.radius) { e.pos.y = kFieldT + e.radius; e.vel.y = std::abs (e.vel.y); if (std::sin (e.wander) < 0.0f) e.wander = -e.wander; }
            if (e.pos.y > kFieldB - e.radius) { e.pos.y = kFieldB - e.radius; e.vel.y = -std::abs (e.vel.y); if (std::sin (e.wander) > 0.0f) e.wander = -e.wander; }
        }

        // Keep the crowd from stacking into one blob: a light shove apart.
        for (size_t i = 0; i < enemies.size(); ++i)
        {
            auto& a = enemies[i];
            if (! a.alive || a.spawn > 0.0f || a.kind == EnemyKind::well) continue;
            for (size_t j = i + 1; j < enemies.size(); ++j)
            {
                auto& b = enemies[j];
                if (! b.alive || b.spawn > 0.0f || b.kind == EnemyKind::well) continue;
                const Vec2 d = a.pos - b.pos;
                const float minD = (a.radius + b.radius) * 0.9f;
                const float dd = d.lengthSq();
                if (dd < minD * minD && dd > 0.01f)
                {
                    const float l = std::sqrt (dd);
                    const Vec2 push = d / l * ((minD - l) * 0.5f);
                    a.pos += push; b.pos -= push;
                }
            }
        }
    }

    //==========================================================================

    void World::updateBullets (float dt)
    {
        for (auto& b : bullets)
        {
            if (! b.alive) continue;

            // Wells bend shots.
            for (const auto& e : enemies)
            {
                if (! e.alive || e.kind != EnemyKind::well || e.spawn > 0.0f) continue;
                const Vec2 d = e.pos - b.pos;
                const float dist = d.length();
                if (dist < 300.0f && dist > 1.0f) b.vel += d / dist * (2600.0f * (1.0f - dist / 300.0f) * dt);
            }

            b.pos += b.vel * dt;
            b.life -= dt;

            if (b.pos.x < kFieldL || b.pos.x > kFieldR || b.pos.y < kFieldT || b.pos.y > kFieldB || b.life <= 0.0f)
            {
                b.alive = false;
                const Vec2 at = clampToField (b.pos, 1.0f);
                for (int i = 0; i < 4; ++i)
                {
                    Particle p;
                    p.pos = at;
                    p.vel = rng.inDisc (380.0f) - b.vel.normalised() * 120.0f;
                    p.color = chordColor (b.chord).withAlpha (0.8f);
                    p.life = p.maxLife = rng.range (0.15f, 0.35f);
                    p.width = 2.0f;
                    particles.push_back (p);
                }
            }
        }
    }

    void World::updateParticles (float dt)
    {
        const float step = dt * 60.0f;

        // Wells drag light in.
        Vec2 wellPos[4]; int wellCount = 0;
        for (const auto& e : enemies)
            if (e.alive && e.kind == EnemyKind::well && e.spawn <= 0.0f && wellCount < 4) wellPos[wellCount++] = e.pos;

        for (auto& p : particles)
        {
            p.life -= dt;
            p.vel *= std::pow (p.drag, step);

            for (int w = 0; w < wellCount; ++w)
            {
                const Vec2 d = wellPos[w] - p.pos;
                const float dist = d.length();
                if (dist < 300.0f && dist > 4.0f)
                    p.vel += (d / dist * 2200.0f + d.perp() / dist * 900.0f) * ((1.0f - dist / 300.0f) * dt);
            }

            p.pos += p.vel * dt;

            if (p.pos.x < kFieldL) { p.pos.x = kFieldL; p.vel.x = std::abs (p.vel.x) * 0.6f; }
            if (p.pos.x > kFieldR) { p.pos.x = kFieldR; p.vel.x = -std::abs (p.vel.x) * 0.6f; }
            if (p.pos.y < kFieldT) { p.pos.y = kFieldT; p.vel.y = std::abs (p.vel.y) * 0.6f; }
            if (p.pos.y > kFieldB) { p.pos.y = kFieldB; p.vel.y = -std::abs (p.vel.y) * 0.6f; }
        }

        particles.erase (std::remove_if (particles.begin(), particles.end(), [] (const Particle& p) { return p.life <= 0.0f; }),
                         particles.end());

        // Past the cap the oldest go first; a frame with ten thousand sparks is
        // a frame that drops.
        if (particles.size() > 7000) particles.erase (particles.begin(), particles.begin() + (particles.size() - 7000));
    }

    void World::updatePickups (float dt)
    {
        for (auto& p : pickups)
        {
            p.life -= dt;
            p.angle += dt * 2.0f;
            p.vel *= std::exp (-dt * 0.8f);

            if (ship.alive)
            {
                const Vec2 d = ship.pos - p.pos;
                const float dist = d.length();
                if (dist < 160.0f && dist > 1.0f) p.vel += d / dist * (1400.0f * dt);   // a little magnet
                if (dist < 38.0f)
                {
                    p.alive = false;
                    band.post ({ Command::powerUp });
                    static const char* names[3] { "ARP 1/8", "ARP 1/16", "ARP 1/32" };
                    const int rate = std::min (2, band.pubArpRate.load() + 1);
                    popups.push_back ({ p.pos, names[rate], 1.4f, kPowerUpColor, 30.0f });
                    explode (p.pos, kPowerUpColor, { 1, 1, 1 }, 60, 600.0f, 0.9f);
                    rings.push_back ({ p.pos, 20.0f, 900.0f, 0.5f, kPowerUpColor });
                }
            }

            p.pos = clampToField (p.pos + p.vel * dt, 20.0f);
            if (p.life <= 0.0f) p.alive = false;
        }
    }

    //==========================================================================

    void World::collide()
    {
        // Bullets into enemies.
        for (auto& b : bullets)
        {
            if (! b.alive) continue;
            for (auto& e : enemies)
            {
                if (! e.alive || e.spawn > 0.0f) continue;
                const float r = e.radius + 6.0f;
                if ((e.pos - b.pos).lengthSq() > r * r) continue;

                b.alive = false;

                if (e.kind == EnemyKind::well)
                {
                    e.hp -= 1.0f;
                    e.flash = 1.0f;
                    e.vel += b.vel * 0.01f;
                    grid.push (e.pos, 120.0f, 120.0f);
                    if (e.hp <= 0.0f) kill (e, b.chord, true, true, b.note);
                }
                else
                {
                    kill (e, b.chord, true, true, b.note);
                }
                break;
            }
        }

        // Enemies into the ship.
        if (state == Phase::playing && ship.alive && ship.invulnerable <= 0.0f)
        {
            for (const auto& e : enemies)
            {
                if (! e.alive || e.spawn > 0.0f) continue;
                const float r = e.radius * 0.8f + 12.0f;
                if ((e.pos - ship.pos).lengthSq() < r * r) { shipDies(); break; }
            }
        }
    }

    void World::kill (Enemy& e, int chord, bool scored, bool sounded, int note)
    {
        if (! e.alive) return;
        e.alive = false;

        const Color ec = enemyColor (e.kind);
        const Color cc = chordColor (chord);
        const bool big = e.kind == EnemyKind::well;

        explode (e.pos, ec, cc, big ? 220 : 70, big ? 1100.0f : 650.0f, big ? 1.8f : 1.1f);
        grid.push (e.pos, big ? 1800.0f : 520.0f, big ? 460.0f : 150.0f);
        if (big) { shakeAmount = std::max (shakeAmount, 10.0f); rings.push_back ({ e.pos, 30.0f, 1400.0f, 0.6f, ec }); }

        if (sounded)
            band.post ({ Command::kill, chord, chordSize (e.kind), e.pos.x / kWorldW, note, (int) e.kind });

        if (scored)
        {
            ++killsSinceDeath;
            multiplier = std::min (10, 1 + killsSinceDeath / 30);
            addScore (e.value * multiplier, e.pos);
            if (! slowActive) slowMeter = std::min (1.0f, slowMeter + kSlowPerKill);   // no refill while it runs

            // Arpeggio power-ups drop now and then while there is room to speed up.
            if (band.pubArpRate.load() < 2 && pickups.empty() && rng.chance (big ? 0.6f : 0.022f))
                pickups.push_back ({ e.pos, rng.inDisc (80.0f), 9.0f, 0.0f, true });
        }

        if (e.kind == EnemyKind::splitter && scored)
            for (int i = 0; i < 3; ++i)
            {
                spawnEnemy (EnemyKind::shard, e.pos + fromAngle (kTau * i / 3.0f + e.angle, 24.0f), 0.0f);
                if (! incoming.empty()) incoming.back().vel = fromAngle (kTau * i / 3.0f + e.angle, 320.0f);
            }
    }

    void World::explode (Vec2 pos, Color a, Color b, int count, float speed, float life)
    {
        for (int i = 0; i < count; ++i)
        {
            Particle p;
            p.pos = pos;
            const float s = speed * (0.15f + 0.85f * std::pow (rng.unit(), 0.6f));
            p.vel = fromAngle (rng.range (0.0f, kTau), s);
            p.color = mix (a, b, rng.unit());
            p.color.a = rng.range (0.6f, 1.0f);
            p.life = p.maxLife = life * rng.range (0.4f, 1.0f);
            p.width = rng.range (1.8f, 3.4f);
            p.drag = rng.range (0.92f, 0.965f);
            particles.push_back (p);
        }
    }

    void World::addScore (int amount, Vec2 where)
    {
        points += amount;
        if (amount >= 200) popups.push_back ({ where, std::to_string (amount), 0.8f, { 1, 1, 1 }, 18.0f });

        if (points >= nextLifeAt) { ++lives; nextLifeAt += kLifeEvery; banner = "+1 LIFE"; bannerLife = 1.6f; bannerColor = { 0.4f, 1.0f, 0.5f }; }
        if (points >= nextBombAt) { ++bombs; nextBombAt += kBombEvery; banner = "+1 BOMB"; bannerLife = 1.6f; bannerColor = kBombColor; }
    }

    void World::shipDies()
    {
        ship.alive = false;
        band.firing = false;
        setSlow (false);
        band.post ({ Command::death });

        explode (ship.pos, { 1, 1, 1 }, chordColor (band.pubChord.load()), 500, 1300.0f, 2.2f);
        explode (ship.pos, { 1.0f, 0.8f, 0.3f }, { 1.0f, 0.2f, 0.1f }, 200, 700.0f, 1.6f);
        grid.push (ship.pos, 2600.0f, 600.0f);
        rings.push_back ({ ship.pos, 20.0f, 1600.0f, 0.8f, { 1, 1, 1 } });
        shakeAmount = 22.0f;
        flashAmount = 0.6f;

        // Everything on the field goes with it, silently and for no score.
        for (auto& e : enemies) if (e.alive) kill (e, 0, false, false);
        pickups.clear();

        multiplier = 1;
        killsSinceDeath = 0;

        if (--lives <= 0)
        {
            state = Phase::gameOver;
            band.post ({ Command::stop });
            banner = "GAME OVER"; bannerLife = 1.0e9f; bannerColor = { 1.0f, 0.25f, 0.25f };
        }
        else
        {
            state = Phase::dead;
            deadTime = 0.0f;
        }
    }

    void World::respawn()
    {
        if (state != Phase::dead) return;
        state = Phase::playing;
        ship.alive = true;
        ship.pos = kCentre;
        ship.vel = {};
        ship.invulnerable = 2.0f;
        rings.push_back ({ ship.pos, 300.0f, -500.0f, 0.55f, chordColor (band.pubChord.load()) });
    }

    void World::detonateBomb()
    {
        if (bombs <= 0 || ! ship.alive) return;
        --bombs;
        band.post ({ Command::bomb });

        for (auto& e : enemies) if (e.alive) kill (e, band.pubChord.load(), false, false);
        grid.push (ship.pos, 3200.0f, 1400.0f);
        rings.push_back ({ ship.pos, 30.0f, 2200.0f, 0.9f, kBombColor });
        rings.push_back ({ ship.pos, 10.0f, 1600.0f, 1.1f, { 1, 1, 1 } });
        explode (ship.pos, kBombColor, { 1.0f, 0.6f, 0.5f }, 400, 1800.0f, 1.6f);
        shakeAmount = 18.0f;
        flashAmount = 0.8f;
        ship.invulnerable = std::max (ship.invulnerable, 1.0f);
    }

    void World::threats (std::vector<Threat>& out) const
    {
        out.clear();
        for (const auto& e : enemies)
            if (e.alive) out.push_back ({ e.pos, e.vel, e.radius, e.spawn <= 0.0f });
    }

    //==========================================================================
    // Drawing

    void World::draw (Neon& neon)
    {
        // Slow motion zooms the field 20% in on the ship, inside its border:
        // the view of the field moves, the frame round it stays whole
        // (2026-10-13). The camera goes no further than keeps the zoomed view
        // filling the field.
        const float slowA = slowAmount();
        {
            // From 100% to 150% over the whole slow motion, logarithmically —
            // the same ratio every moment, so it feels like a steady push in
            // rather than one that slows down (150% since 2026-10-13, 120%
            // before). It reaches 150% as the meter runs dry and eases back out
            // when it ends.
            const float zoom = std::pow (1.5f, slowProgress * slowA);
            const Vec2 mid { (kFieldL + kFieldR) * 0.5f, (kFieldT + kFieldB) * 0.5f };
            const float hw = (kFieldR - kFieldL) * 0.5f / zoom, hh = (kFieldB - kFieldT) * 0.5f / zoom;
            const Vec2 target = ship.alive ? Vec2 { std::clamp (ship.pos.x, kFieldL + hw, kFieldR - hw),
                                                    std::clamp (ship.pos.y, kFieldT + hh, kFieldB - hh) } : mid;
            neon.setCamera (lerp (mid, target, slowA), zoom, mid);
            neon.radialCentre = ship.alive ? ship.pos : mid;
            neon.beginClip (kFieldL, kFieldT, kFieldR, kFieldB);
        }

        const Color tint = levelTint (level);
        grid.draw (neon, tint, kickPulse);

        for (const auto& p : particles)
        {
            const float a = std::clamp (p.life / p.maxLife, 0.0f, 1.0f);
            const float speed = p.vel.length();
            const Vec2 tail = p.pos - p.vel * std::clamp (0.03f, 0.0f, 40.0f / std::max (1.0f, speed));
            neon.line (tail, p.pos, p.color.withAlpha (a * p.color.a), p.width);
        }

        for (const auto& r : rings)
            if (r.radius > 0.0f) neon.circle (r.pos, r.radius, r.color.withAlpha (std::clamp (r.life * 1.5f, 0.0f, 1.0f)), 5.0f, 64);

        for (const auto& p : pickups)
        {
            const float blink = p.life < 2.0f ? (std::sin (p.life * 25.0f) > 0.0f ? 1.0f : 0.25f) : 1.0f;
            const Color c = kPowerUpColor.withAlpha (blink);
            Vec2 tri[3];
            for (int i = 0; i < 3; ++i) tri[i] = p.pos + fromAngle (p.angle + kTau * i / 3.0f, 18.0f);
            neon.neonPoly (tri, 3, true, c, 3.5f);
            neon.circle (p.pos, 26.0f + 3.0f * kickPulse, c.withAlpha (0.6f * blink), 2.5f, 24);
            neon.glow (p.pos, 70.0f, c.withAlpha (0.25f * blink));
        }

        for (const auto& e : enemies) if (e.alive) drawEnemy (neon, e);

        for (const auto& b : bullets)
        {
            const Color c = chordColor (b.chord);
            const Vec2 dir = b.vel.normalised();
            const float len = b.accent ? 26.0f : 18.0f;
            neon.neonLine (b.pos - dir * len, b.pos, c, b.accent ? 5.5f : 4.0f);
        }

        if (ship.alive) drawShip (neon);

        for (const auto& p : popups)
            neon.text (p.text.c_str(), p.pos, p.size, p.color.withAlpha (std::clamp (p.life * 2.0f, 0.0f, 1.0f)), 0.5f);

        neon.endClip();
        neon.resetCamera();

        // Field border, pumping with the kick, outside the zoom. In slow
        // motion it turns the meter's blue and thickens.
        const Vec2 border[4] { { kFieldL, kFieldT }, { kFieldR, kFieldT }, { kFieldR, kFieldB }, { kFieldL, kFieldB } };
        neon.neonPoly (border, 4, true, mix (levelAccent (level), Color { 0.55f, 0.75f, 1.0f }, slowA)
                                            .withAlpha (0.55f + 0.35f * kickPulse + 0.3f * slowA), 4.0f + 4.0f * slowA);

        neon.beginOverlay();
        drawHud (neon);
        neon.endOverlay();
    }

    void World::drawEnemy (Neon& neon, const Enemy& e) const
    {
        Color c = enemyColor (e.kind);
        float scale = 1.0f;
        float alpha = 1.0f;

        if (e.spawn > 0.0f)
        {
            // Spawning in: grows down from large and flickers — visible, harmless.
            scale = 1.0f + e.spawn * 2.2f;
            alpha = (std::fmod (e.spawn * 14.0f, 1.0f) > 0.4f ? 0.75f : 0.25f);
        }

        if (e.flash > 0.0f) c = mix (c, { 1, 1, 1 }, e.flash);
        c.a = alpha;

        const float r = e.radius * scale;
        const Vec2 p = e.pos;

        switch (e.kind)
        {
            case EnemyKind::wanderer:
            {
                // A pinwheel: four blades.
                for (int i = 0; i < 4; ++i)
                {
                    const float a = e.angle + kPi * 0.5f * i;
                    const Vec2 pts[3] { p, p + fromAngle (a, r), p + fromAngle (a + 0.9f, r * 0.75f) };
                    neon.neonPoly (pts, 3, true, c, 3.2f);
                }
                break;
            }

            case EnemyKind::seeker:
            {
                // A diamond that breathes on the kick.
                const float k = 1.0f + 0.18f * kickPulse;
                const Vec2 pts[4] { p + Vec2 { 0, -r * 1.2f * k }, p + Vec2 { r * 0.8f * k, 0 },
                                    p + Vec2 { 0, r * 1.2f * k }, p + Vec2 { -r * 0.8f * k, 0 } };
                neon.neonPoly (pts, 4, true, c, 3.6f);
                break;
            }

            case EnemyKind::dodger:
            {
                Vec2 outer[4], inner[4];
                for (int i = 0; i < 4; ++i)
                {
                    outer[i] = p + fromAngle (e.angle + kPi * 0.25f + kPi * 0.5f * i, r * 1.2f);
                    inner[i] = p + fromAngle (-e.angle + kPi * 0.5f * i, r * 0.55f);
                }
                neon.neonPoly (outer, 4, true, c, 3.4f);
                neon.polyline (inner, 4, true, c, 2.4f);
                break;
            }

            case EnemyKind::splitter:
            {
                Vec2 sq[4];
                for (int i = 0; i < 4; ++i) sq[i] = p + fromAngle (e.angle + kPi * 0.25f + kPi * 0.5f * i, r * 1.25f);
                neon.neonPoly (sq, 4, true, c, 3.6f);
                neon.line (sq[0], sq[2], c, 2.4f);
                neon.line (sq[1], sq[3], c, 2.4f);
                break;
            }

            case EnemyKind::shard:
            {
                Vec2 sq[4];
                for (int i = 0; i < 4; ++i) sq[i] = p + fromAngle (e.angle + kPi * 0.5f * i, r * 1.2f);
                neon.neonPoly (sq, 4, true, c, 2.8f);
                break;
            }

            case EnemyKind::well:
            {
                const float k = 1.0f + 0.15f * kickPulse;
                neon.glow (p, r * 4.0f, c.withAlpha (0.35f * alpha));
                neon.circle (p, r * k, c, 4.5f, 40);
                neon.circle (p, r * 0.62f * k, c.withAlpha (alpha * 0.8f), 3.0f, 32);
                neon.circle (p, r * 1.5f + 6.0f * std::sin (time * 6.0f), c.withAlpha (alpha * 0.35f), 2.0f, 40);
                break;
            }
        }
    }

    void World::drawShip (Neon& neon) const
    {
        if (ship.invulnerable > 0.0f && std::fmod (ship.invulnerable * 10.0f, 1.0f) < 0.35f) return;

        const Color chordC = chordColor (band.pubChord.load());
        const Vec2 local[5] { { 24, 0 }, { -14, 16 }, { -6, 0 }, { -14, -16 }, { 24, 0 } };
        Vec2 pts[5];
        for (int i = 0; i < 5; ++i) pts[i] = ship.pos + rotate (local[i], ship.angle);

        neon.glow (ship.pos, 70.0f, chordC.withAlpha (0.10f + 0.08f * kickPulse));
        neon.neonPoly (pts, 4, true, Color { 1.0f, 0.95f, 0.8f }, 4.2f);
        neon.polyline (pts, 4, true, chordC.withAlpha (0.9f), 9.0f);

        // A thin aim line so you can see where the arpeggio will go.
        if (band.firing.load())
            neon.line (ship.pos + aimDir * 30.0f, ship.pos + aimDir * 70.0f, chordC.withAlpha (0.35f), 2.0f);
    }

    void World::drawWheel (Neon& neon, Vec2 centre) const
    {
        const auto& song = audio::song (level);
        const int playing = band.pubChord.load();
        const int wanted = band.requestedChord.load();
        const float radius = 84.0f;

        neon.circle (centre, radius, levelAccent (level).withAlpha (0.45f), 2.5f, 48);

        static const Vec2 dirs[4] { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
        for (int s = 0; s < 4; ++s)
        {
            char name[8];
            audio::chordName (song, s, name, sizeof (name));
            const Vec2 at = centre + dirs[s] * radius;
            const Color c = chordColor (s);

            if (s == playing)
            {
                neon.glow (at, 40.0f + 20.0f * chordFlash, c.withAlpha (0.10f + 0.30f * chordFlash));
                neon.text (name, at + Vec2 { 0, -16 }, 32.0f, c, 0.5f, 3.8f);
            }
            else
            {
                const bool pending = s == wanted;
                const float blink = pending ? (0.5f + 0.5f * std::sin (time * 18.0f)) : 0.0f;
                neon.text (name, at + Vec2 { 0, -12 }, 24.0f, c.withAlpha (0.55f + 0.45f * blink), 0.5f, 2.8f);
            }
        }

        // The needle: where the stick points.
        neon.line (centre, centre + dirs[wanted] * (radius * 0.55f), chordColor (wanted).withAlpha (0.8f), 3.0f);
    }

    void World::drawHud (Neon& neon) const
    {
        const Color white { 1, 1, 1 };
        const Color accent = levelAccent (level);
        const auto& song = audio::song (level);

        // Top band: score, song and progress, lives and bombs.
        char buf[64];
        std::snprintf (buf, sizeof (buf), "%d", points);
        neon.text (buf, { 40, 22 }, 38.0f, white, 0.0f, 3.4f);
        std::snprintf (buf, sizeof (buf), "x%d", multiplier);
        neon.text (buf, { 40 + Neon::textWidth (std::to_string (points).c_str(), 38.0f) + 24.0f, 34 }, 24.0f, accent, 0.0f);

        // Slow-motion meter, beside the score: a framed bar with its name
        // above. Dim when it cannot start yet, bright and pulsing while it
        // runs (bigger since 2026-10-13).
        {
            const Color slowC { 0.55f, 0.75f, 1.0f };
            const float x0 = 360.0f, x1 = 650.0f, y0 = 30.0f, y1 = 56.0f, ym = (y0 + y1) * 0.5f;   // clear of a 6-digit score and of the song bar
            const float a = slowActive ? 0.8f + 0.2f * std::sin (time * 20.0f) : (slowMeter > 0.15f ? 0.8f : 0.35f);
            const Vec2 frame[4] { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } };
            neon.polyline (frame, 4, true, slowC.withAlpha (0.45f), 2.0f);
            if (slowMeter > 0.0f)
                neon.line ({ x0 + 6.0f, ym }, { lerp (x0 + 6.0f, x1 - 6.0f, slowMeter), ym }, slowC.withAlpha (a), 11.0f);
            neon.text ("SLOW MOTION", { x0, 8.0f }, 15.0f, slowC.withAlpha (std::max (a, 0.55f)), 0.0f);
        }

        const std::string title = std::string (song.name) + " · " + difficulty (diff).name;
        neon.text (title.c_str(), { kWorldW * 0.5f, 16 }, 28.0f, accent, 0.5f, 3.0f);

        const double beats = band.beatPosition();
        const float progress = std::clamp ((float) (beats / 4.0 / song.lengthBars), 0.0f, 1.0f);
        const float bx0 = kWorldW * 0.5f - 260.0f, bx1 = kWorldW * 0.5f + 260.0f;
        neon.line ({ bx0, 60 }, { bx1, 60 }, accent.withAlpha (0.25f), 3.0f);
        neon.line ({ bx0, 60 }, { lerp (bx0, bx1, progress), 60 }, accent, 4.0f);

        for (int i = 0; i < lives; ++i)
        {
            const Vec2 c { kWorldW - 60.0f - 44.0f * i, 36.0f };
            const Vec2 pts[4] { c + Vec2 { 0, -14 }, c + Vec2 { 11, 10 }, c + Vec2 { 0, 3 }, c + Vec2 { -11, 10 } };
            neon.polyline (pts, 4, true, white, 3.0f);
        }
        for (int i = 0; i < bombs; ++i)
            neon.circle ({ kWorldW - 420.0f + 34.0f * i, 36.0f }, 11.0f, kBombColor, 3.5f, 16);

        // Bottom left: the wheel.
        drawWheel (neon, { 150.0f, kFieldB - 140.0f });

        // Bottom right: the arrangement — which layers are in, and the arpeggio.
        const int layer = band.pubLayer.load();
        const float lx = kFieldR - 290.0f, ly = kFieldB - 35.0f - 26.0f * (float) audio::maxLayer (song);
        const int layers = audio::maxLayer (song);
        for (int i = 0; i <= layers; ++i)
        {
            const bool on = i <= layer;
            const float y = ly + 26.0f * i;
            neon.line ({ lx, y }, { lx + 16, y }, accent.withAlpha (on ? 1.0f : 0.25f), on ? 6.0f : 3.0f);
            neon.text (audio::layerName (song, i), { lx + 28, y - 9 }, 18.0f, white.withAlpha (on ? 0.95f : 0.4f), 0.0f);
        }

        if (layer < layers && state == Phase::playing)
        {
            std::snprintf (buf, sizeof (buf), "NEXT IN %d BARS", band.pubBarsToLayer.load());
            neon.text (buf, { lx, ly - 34 }, 17.0f, accent.withAlpha (0.8f), 0.0f);
        }

        static const char* rates[3] { "ARP 1/8", "ARP 1/16", "ARP 1/32" };
        const int rate = band.pubArpRate.load();
        neon.text (rates[rate], { lx, ly - 124 }, 26.0f, rate > 0 ? kPowerUpColor : white.withAlpha (0.6f), 0.0f, 2.4f);
        if (song.randomArp)   // this start's direction, on its own line so it fits
            neon.text (audio::arpDirectionName (band.pubArpDir.load()), { lx, ly - 90 }, 18.0f, white.withAlpha (0.7f), 0.0f);
        const int left = band.pubRateBars.load();
        for (int i = 0; i < left; ++i)
            neon.line ({ lx + 14.0f * i, ly - 58 }, { lx + 8 + 14.0f * i, ly - 58 }, kPowerUpColor, 4.0f);

        // Banner in the middle: layers in and out, the song name, the end.
        if (bannerLife > 0.0f)
        {
            const float a = std::clamp (bannerLife * 1.5f, 0.0f, 1.0f);
            neon.text (banner.c_str(), { kWorldW * 0.5f, kWorldH * 0.40f }, 64.0f, bannerColor.withAlpha (a), 0.5f, 5.0f);
        }

        if (state == Phase::cleared || state == Phase::gameOver)
        {
            std::snprintf (buf, sizeof (buf), "SCORE %d", points);
            neon.text (buf, { kWorldW * 0.5f, kWorldH * 0.40f + 100.0f }, 40.0f, white, 0.5f, 3.4f);
            neon.text ("PRESS A", { kWorldW * 0.5f, kWorldH * 0.40f + 170.0f }, 24.0f,
                           white.withAlpha (0.5f + 0.5f * std::sin (time * 4.0f)), 0.5f, 2.4f);
        }
    }
}
