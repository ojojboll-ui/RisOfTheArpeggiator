#pragma once

#include <SDL3/SDL.h>

#include "Math.h"
#include "World.h"

/**
    Pad first, keyboard and mouse as a fallback.

    Any SDL gamepad works — Xbox, PlayStation, the Steam Controller with Steam
    closed (see sdl3-motion-controllers), Joy-Con. Left stick moves and picks
    the chord, right stick aims and plays the arpeggio, any shoulder or trigger
    is a bomb. Without a pad: WASD moves, the arrow keys or the mouse aim and
    fire, space is the bomb.
*/
namespace np
{
    struct MenuKeys
    {
        bool up = false, down = false, left = false, right = false;
        bool confirm = false, back = false, pause = false;
    };

    class Input
    {
    public:
        void handleEvent (const SDL_Event& e, SDL_Renderer* renderer);
        void beginFrame();                      // clears this frame's presses

        /** Gameplay controls. Mouse aiming needs the ship and the world-space
            cursor. */
        Controls controls (Vec2 shipPos) const;
        const MenuKeys& menu() const { return keys; }

        void setMouseWorld (Vec2 w) { mouseWorld = w; }
        Vec2 mouseWindowPixels() const { return mousePixels; }

        bool hasPad() const { return pad != nullptr; }
        void rumble (float low, float high, int ms);
        void shutdown();

    private:
        static Vec2 deadzone (Vec2 v, float inner);
        Vec2 stick (SDL_GamepadAxis x, SDL_GamepadAxis y) const;
        float axis (SDL_GamepadAxis a) const;

        SDL_Gamepad* pad = nullptr;
        MenuKeys keys;
        bool bombPressed = false;
        bool lastStickNav = false;
        bool triggerHeld = false;

        bool kW = false, kA = false, kS = false, kD = false;
        bool kUp = false, kDown = false, kLeft = false, kRight = false;
        bool mouseDown = false, kShift = false;
        Vec2 mousePixels, mouseWorld;
    };
}
