# Rise of the Arpeggiator

A neon twin-stick shooter where the music *is* the game. The left stick flies
the ship and picks one of four chords; the right stick aims and plays the
arpeggio — every note of the arpeggio is a shot. Enemies die in chords and
notes, and the track builds from a lone kick drum into the full song for as
long as you stay alive. Die, and it loses a layer.

Three songs, each about two and a half minutes:

| Song | Style | Tempo | Wheel (up, right, down, left) |
|---|---|---|---|
| PULSE | Juno house | 122 BPM | Am, F, C, G |
| ACID | 303 acid | 130 BPM | Em, C, G, D |
| STARFALL | Trance | 138 BPM | E, F#m, Bm, A |

Survive to the end of the song to clear it. Easy, normal and hard are chosen
in the menu; best scores are kept per song and difficulty.

## Controls

Any SDL3 gamepad works (Xbox, PlayStation, Switch, Steam Controller with Steam
closed). Keyboard and mouse work too.

| | Gamepad | Keyboard and mouse |
|---|---|---|
| Fly, and pick the chord | Left stick | W A S D |
| Aim and fire (plays the arpeggio) | Right stick | Arrow keys, or hold the left mouse button |
| Bomb | RT, LB or RB | Space, or right mouse button |
| Slow motion (hold) | LT | Shift |
| Pause | Start | Esc or P |
| Confirm | A | Enter |
| Back | B | Backspace |
| Fullscreen | | F11 |

In the menu: left and right choose the song, up and down the difficulty.

**The chord wheel.** Pushing the left stick clearly towards a direction picks
that direction's chord; it changes on the next beat, so dodging never breaks
the music. Circling the stick clockwise plays the song's progression. Bass,
arpeggio and the sounds of dying enemies all follow the chord you are on.

**Fire rate is the arpeggio.** Shots leave on the arpeggio's notes, at 1/8 to
begin with. Green power-ups speed it up to 1/16 and 1/32 for eight bars each.

**Slow motion** drains the meter beside the score and refills it with kills
while it is not running. The field slows to 30%, zooms in on the ship and the
music sinks under a filter.

**Bombs** clear the field. The blast is immediate; its sound lands on the
next 32nd note, in time with the music.

## Building

Windows, with:

- Visual Studio 2022 or its Build Tools, with the C++ workload
- CMake 3.22 or newer
- Git (CMake fetches SDL3 3.4.16 from GitHub on the first configure)

From the repository folder:

```bat
build.bat
```

It configures and builds in `build\` and copies `RiseOfTheArpeggiator.exe`
next to itself. SDL3 is linked statically, so the exe runs on its own.
`build.bat C:\somewhere` builds in another folder instead; a short path helps
if MSBuild complains about path length. It uses Visual Studio 2022 unless the
`CMAKE_GENERATOR` environment variable names another generator.

Or by hand:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

which leaves the game in `build\Release\`.

## Offline tests

The audio engine is plain C++ with no SDL, so it can be tested without a
window or a gamepad. From `tools` (the script finds Visual Studio's C++
compiler itself with `vswhere`):

```bat
build.bat audiotest ..\out
```

`audiotest` plays every song with scripted input at two buffer sizes and
requires bit-identical output, checks the layers, deaths, drops, slow motion
and build-ups, measures every bus, and writes a WAV per song.
`build.bat padpreview ..\out` renders STARFALL's pad round the chord wheel with
different chords in one slot, for choosing by ear.

The game itself takes a few flags for testing:

| Flag | |
|---|---|
| `--demo` | the autopilot plays |
| `--level N`, `--difficulty easy\|normal\|hard` | which song and difficulty |
| `--seconds T` | quit after T seconds |
| `--shots DIR`, `--every S` | save a PNG every S seconds |
| `--wav PATH`, `--log PATH` | record the audio, write timing and events |
| `--slowtest`, `--reckless` | autopilot holds slow motion now and then / flies unarmed to test deaths |
| `--fullscreen`, `--novsync` | |

## Where the sounds come from

The synth voices are borrowed from two earlier instruments in the same
family, Chordtroller and Attract Mode: the Juno voice and chorus, the 303, the
Omnichord plucks and the music-box pling. The drums, reverb, riser and the
conductor that runs the band are new. Everything is synthesised; there are no
samples.
