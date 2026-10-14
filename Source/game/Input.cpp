#include "Input.h"

namespace np
{
    void Input::beginFrame()
    {
        keys = {};
        bombPressed = false;

        // Triggers are axes, so a bomb on them is an edge on a threshold.
        if (pad)
        {
            // RT bombs; LT is slow motion now (2026-10-10), read as held in controls().
            const bool held = axis (SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.6f;
            if (held && ! triggerHeld) bombPressed = true;
            triggerHeld = held;

            // The left stick navigates menus as well, one step per push.
            const Vec2 s = stick (SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY);
            const bool pushed = s.length() > 0.6f;
            if (pushed && ! lastStickNav)
            {
                if (std::abs (s.x) > std::abs (s.y)) (s.x > 0 ? keys.right : keys.left) = true;
                else                                 (s.y > 0 ? keys.down : keys.up) = true;
            }
            lastStickNav = pushed;
        }
    }

    void Input::handleEvent (const SDL_Event& e, SDL_Renderer* renderer)
    {
        switch (e.type)
        {
            case SDL_EVENT_GAMEPAD_ADDED:
                if (! pad) pad = SDL_OpenGamepad (e.gdevice.which);
                break;

            case SDL_EVENT_GAMEPAD_REMOVED:
                if (pad && SDL_GetGamepadID (pad) == e.gdevice.which)
                {
                    SDL_CloseGamepad (pad);
                    pad = nullptr;

                    // Another one may still be plugged in.
                    int count = 0;
                    if (SDL_JoystickID* ids = SDL_GetGamepads (&count))
                    {
                        if (count > 0) pad = SDL_OpenGamepad (ids[0]);
                        SDL_free (ids);
                    }
                }
                break;

            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                switch (e.gbutton.button)
                {
                    case SDL_GAMEPAD_BUTTON_SOUTH:          keys.confirm = true; break;
                    case SDL_GAMEPAD_BUTTON_EAST:           keys.back = true; break;
                    case SDL_GAMEPAD_BUTTON_START:          keys.pause = true; break;
                    case SDL_GAMEPAD_BUTTON_BACK:           keys.back = true; break;
                    case SDL_GAMEPAD_BUTTON_DPAD_UP:        keys.up = true; break;
                    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:      keys.down = true; break;
                    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:      keys.left = true; break;
                    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:     keys.right = true; break;
                    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
                    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: bombPressed = true; break;
                    default: break;
                }
                break;

            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
            {
                const bool down = e.type == SDL_EVENT_KEY_DOWN;
                switch (e.key.scancode)
                {
                    case SDL_SCANCODE_W: kW = down; if (down && ! e.key.repeat) keys.up = true; break;
                    case SDL_SCANCODE_A: kA = down; if (down && ! e.key.repeat) keys.left = true; break;
                    case SDL_SCANCODE_S: kS = down; if (down && ! e.key.repeat) keys.down = true; break;
                    case SDL_SCANCODE_D: kD = down; if (down && ! e.key.repeat) keys.right = true; break;
                    case SDL_SCANCODE_UP:    kUp = down;    if (down && ! e.key.repeat) keys.up = true; break;
                    case SDL_SCANCODE_DOWN:  kDown = down;  if (down && ! e.key.repeat) keys.down = true; break;
                    case SDL_SCANCODE_LEFT:  kLeft = down;  if (down && ! e.key.repeat) keys.left = true; break;
                    case SDL_SCANCODE_RIGHT: kRight = down; if (down && ! e.key.repeat) keys.right = true; break;
                    case SDL_SCANCODE_RETURN:
                    case SDL_SCANCODE_KP_ENTER: if (down && ! e.key.repeat) keys.confirm = true; break;
                    case SDL_SCANCODE_SPACE:
                    case SDL_SCANCODE_LSHIFT:
                    case SDL_SCANCODE_RSHIFT:   kShift = down; break;
                    case SDL_SCANCODE_ESCAPE:
                    case SDL_SCANCODE_P:        if (down && ! e.key.repeat) keys.pause = true; break;
                    case SDL_SCANCODE_BACKSPACE: if (down && ! e.key.repeat) keys.back = true; break;
                    default: break;
                }
                break;
            }

            case SDL_EVENT_MOUSE_MOTION:
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                SDL_Event c = e;
                SDL_ConvertEventToRenderCoordinates (renderer, &c);
                if (e.type == SDL_EVENT_MOUSE_MOTION) mousePixels = { c.motion.x, c.motion.y };
                else
                {
                    mousePixels = { c.button.x, c.button.y };
                    if (c.button.button == SDL_BUTTON_LEFT) mouseDown = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                    if (c.button.button == SDL_BUTTON_RIGHT && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) bombPressed = true;
                }
                break;
            }

            default: break;
        }
    }

    float Input::axis (SDL_GamepadAxis a) const
    {
        if (! pad) return 0.0f;
        return (float) SDL_GetGamepadAxis (pad, a) / 32767.0f;
    }

    Vec2 Input::stick (SDL_GamepadAxis x, SDL_GamepadAxis y) const
    {
        return { axis (x), axis (y) };   // SDL: +y is down, same as the screen
    }

    Vec2 Input::deadzone (Vec2 v, float inner)
    {
        // Radial, and rescaled so the travel past the deadzone is the whole range.
        const float m = v.length();
        if (m < inner) return {};
        return v / m * std::min (1.0f, (m - inner) / (1.0f - inner));
    }

    Controls Input::controls (Vec2 shipPos) const
    {
        Controls c;

        // Movement: the pad if it is being used, else the keys.
        const Vec2 padMove = deadzone (stick (SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY), 0.18f);
        const Vec2 keyMove { (kD ? 1.0f : 0.0f) - (kA ? 1.0f : 0.0f), (kS ? 1.0f : 0.0f) - (kW ? 1.0f : 0.0f) };
        c.move = padMove.length() > 0.0f ? padMove : keyMove.normalised();

        // Aim: right stick, then arrow keys, then a held mouse button.
        const Vec2 padAim = deadzone (stick (SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY), 0.25f);
        const Vec2 keyAim { (kRight ? 1.0f : 0.0f) - (kLeft ? 1.0f : 0.0f), (kDown ? 1.0f : 0.0f) - (kUp ? 1.0f : 0.0f) };

        if (padAim.length() > 0.0f)       { c.aim = padAim; c.fire = true; }
        else if (keyAim.length() > 0.0f)  { c.aim = keyAim.normalised(); c.fire = true; }
        else if (mouseDown)               { c.aim = (mouseWorld - shipPos).normalised(); c.fire = true; }

        c.bomb = bombPressed;
        c.slow = axis (SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 0.4f || kShift;
        return c;
    }

    void Input::rumble (float low, float high, int ms)
    {
        if (pad)
            SDL_RumbleGamepad (pad, (Uint16) (std::clamp (low, 0.0f, 1.0f) * 65535.0f),
                               (Uint16) (std::clamp (high, 0.0f, 1.0f) * 65535.0f), (Uint32) ms);
    }

    void Input::shutdown()
    {
        if (pad) SDL_CloseGamepad (pad);
        pad = nullptr;
    }
}
