# GCL

GCL is GnuchanOS's own language, and this repository is everything that makes it
work: the interpreter, the IDE, the standard modules, and the bindings that let
GCL, Lua and Python all drive the same raylib window. If you only want to run
something, skip to [Running](#running) at the bottom. The rest of this file is
the reference.

A word on the structure, because it is not the shape people expect. There are
four surfaces here and they are deliberately separate:

- **GCL + Raylib** — the language and its native modules (this is the main one).
- **Lua + LuaRaylib** — a real Lua 5.4 with a raylib binding.
- **Python + PyRaylib** — a real Python 3.14 with a raylib binding.
- **The GCL language itself** — the syntax, the types, the preprocessor.

They can all run at once and talk to each other; the pong demo in `examples/`
does exactly that, with GCL as the referee and a Lua window playing against a
Python window. That is why the bindings get their own sections below.

---

# 1. GCL + Raylib

Raylib is the whole reason GCL exists in its current form. The IDE is a raylib
program, the demo is a raylib program, and every graphics module is built on
top of it. Raylib itself sits on GLFW, and this build uses GLFW's **X11**
backend, so the window is an X11 window — not because any of this code talks to
Xlib, but because that is the platform GLFW was built for here.

Pull it in with:

```
#native <Raylib>
```

and then call everything with the `Raylib.` prefix. The prefix is not optional.
`InitWindow(...)` on its own resolves to nothing and the run stops with "unknown
member". `Raylib.InitWindow(...)` is the call.

```
Raylib.InitWindow(1024, 768, "GCL Project");
while (!Raylib.WindowShouldClose()) {
    Raylib.BeginDrawing();
    Raylib.ClearBackground(Raylib.BLUE);
    Raylib.DrawText("hello", 20, 20, 24, Raylib.WHITE);
    Raylib.EndDrawing();
}
Raylib.CloseWindow();
```

The module is large — on the order of a thousand members covering window and
core, cursor, drawing modes, input, 2D shapes, textures and images, text and
fonts, 3D models, audio, and the colour helpers. There are three conventions you
have to know before any of it makes sense.

## Colours are one packed number

A colour is a single 32-bit integer laid out as `R | G<<8 | B<<16 | A<<24`, and
it travels through the module as one `double`. You do not build a struct. You
either use a constant —

```
Raylib.RED   Raylib.WHITE   Raylib.BLUE   Raylib.RAYWHITE   ...
```

— build one with `Raylib.Color(r, g, b, a)`, or adjust an existing one with
`Raylib.Fade`, `Raylib.ColorAlpha`, `Raylib.ColorBrightness` and friends. All of
them give you back the same packed number, and anything that wants a colour
takes it directly.

## Structs flatten into plain numbers

`Rectangle` is four numbers, `Vector2` is two, `Vector3` is three, `Color` is
the one packed number. So `Raylib.DrawRectangleRec` is really called as
`(x, y, w, h, color)`. If you have a real struct variable it expands on its own:

```
Raylib.Rectangle r = { 10, 10, 100, 50 };
Raylib.DrawRectangleRec(r, Raylib.RED);
```

Nested structs flatten recursively. `&r` does the same thing — the unary `&`
here means "unroll this struct", not "take an address"; there is no pointer
involved.

**Field order is argument order.** If you change the order of the fields in a
raylib struct on the C side, the native side breaks. That is the contract.

## Return values: handles and `Last*` accessors

The module ABI carries only a `double`, so anything that returns a struct (a
texture, a mesh, a camera matrix, a collision result) either comes back as a
numeric **handle** you pass to the matching draw or unload call, or it stashes
the value in an internal slot you read with a `Last*` accessor. Strings work the
same way.

```
Raylib.MeasureTextEx(font, "score", 20, 1);   // fills the Vector2 slot
float w = Raylib.LastV2X();
float h = Raylib.LastV2Y();

Raylib.GetClipboardText();                     // fills the string slot
int n = Raylib.LastStringLen();
for (int i = 0; i < n; i++) {
    int ch = Raylib.LastStringByte(i);         // byte by byte, UTF-8
}
```

The accessor groups, by what the call returns:

| Returned | Read it with |
| --- | --- |
| int | `LastInt()` |
| Vector2 / Vector3 / Vector4 | `LastV2X/Y`, `LastV3X/Y/Z`, `LastV4X/Y/Z/W` |
| Rectangle | `LastRectX/Y/W/H` |
| Ray | `LastRayX/Y/Z`, `LastRayDirX/Y/Z` |
| RayCollision | `LastRayColHit/Distance`, `LastRayColPX/PY/PZ`, `LastRayColNX/NY/NZ` |
| BoundingBox | `LastBoxMinX/Y/Z`, `LastBoxMaxX/Y/Z` |
| NPatchInfo | `LastNPatchX/Y/W/H`, `LastNPatchLeft/Top/Right/Bottom/Layout` |
| GlyphInfo | `LastGlyphValue/OffsetX/OffsetY/AdvanceX`, `LastGlyphImgWidth/ImgHeight` |
| Camera / Camera2D | `LastCamPosX/Y/Z`, `LastCamTargetX/Y/Z`, `LastCamUpX/Y/Z`, `LastCamFovy/Projection`, `LastCam2DOffsetX/Y`, `LastCam2DTargetX/Y`, `LastCam2DRotation/Zoom` |
| Quaternion / Transform | `LastQuatX/Y/Z/W`, `LastTransformTX/TY/TZ`, `LastTransformRX/RY/RZ`, `LastTransformSX/SY/SZ` |
| Matrix | `LastMatrix(i)`, i is 0..15 |
| string | `LastStringLen()`, `LastStringByte(i)` |

Booleans and result codes need no conversion; they arrive as `0` or `1`, so
`int hit = Raylib.CheckCollisionPointRec(mouse, box);` works directly.

## The wrapper modules

Raylib alone gives you drawing primitives. The interesting stuff is in the
modules built on top of it, and this is where the demo (`examples/fps_first_demo/`)
gets its features. Each is its own `#native`.

### `RaylibFPS` — first-person camera and movement

```
#native <RaylibFPS>

RaylibFPS.FPS PLAYER;
Raylib.Camera3D CurrentCamera = PLAYER.Camera;
PLAYER.CameraFOV = 90;
PLAYER.CameraSpeed     = 1.0f;   // how fast the VIEW turns (mouse)
PLAYER.CameraMoveSpeed = 1.0f;   // how fast you WALK (WASD)
PLAYER.Position = { 0, 5, 0 };
PLAYER.Height = 1.8f;

// in the loop:
PLAYER.Move();
PLAYER.Look();
```

`CameraSpeed` and `CameraMoveSpeed` are two separate multipliers on purpose —
one is the look speed, the other the walk speed. Both default to 1.0 (the
documented speed); a value of 0 or negative falls back to the default, because a
mouse that does not turn and a player that does not move are bugs, not settings.

### `RaylibSimpleMesh` — models and terrain from OBJ

```
#native <RaylibSimpleMesh>

RaylibSimpleMesh.Terrain Terrain = RaylibSimpleMesh.TerrainLoad("terrain.obj");
Terrain.Material.Texture = Raylib.LoadTexture("terrain.png");
Terrain.Material.Color   = Raylib.WHITE;

RaylibSimpleMesh.Mesh CUBE = RaylibSimpleMesh.LoadObj("cube.obj");
CUBE.Position = { 0, 0, 0 };
CUBE.Rotate.y = 45;                    // degrees
CUBE.Material.Texture = Raylib.LoadTexture("box.png");
```

Unload with `RaylibSimpleMesh.TerrainUnload("terrain.obj")` and
`RaylibSimpleMesh.UnloadObj(CUBE)`.

### `RaylibSimpleCollision` — walking on terrain, hitting a box

```
#native <RaylibSimpleCollision>

RaylibSimpleCollision.TerrainCollision(PLAYER, Terrain);   // terrain + ground
RaylibSimpleCollision.SimpleBoxCollision(PLAYER, CUBE);    // capsule vs box
```

The player is treated as a capsule `Player.Height` long and 0.3 wide.
`SimpleBoxCollision` resolves against the box's **triangles**, with
`Position`/`Rotate`/`Scale` applied at the call — so a box you have rotated is
represented correctly. The collision functions do not move the player; they only
push a capsule that has ended up inside back out.

### `RaylibSimpleLight` — sun and point lights

```
#native <RaylibSimpleLight>

RaylibSimpleLight.SunLight SUN = RaylibSimpleLight.CreateSun(Raylib.WHITE);
SUN.Rotate = { 0, 0, 90 };

RaylibSimpleLight.SunLight light = RaylibSimpleLight.CreatePointLight(
    Raylib.RED, SUN, 5, 5, 0);      // colour, sun, x, y, z

// in the loop:
SUN.Update();
light.Update();
// inside BeginMode3D:
light.Draw();                        // a glowing cube
```

Use white for sunlight. A saturated sun colour multiplies the terrain texture
channel by channel, so a yellow sun (255,255,0) zeroes the blue channel and
turns every white texel yellow.

### `RaylibShader` — the lighting shader

```
#native <RaylibShader>

int glsl_ver = Raylib.GetGLSLVersion();
gcChar vs = "lighting.vs";
gcChar fs = "lighting.fs";
if (glsl_ver < 330) { vs = "lighting_gl2.vs"; fs = "lighting_gl2.fs"; }
RaylibShader.SimpleShader shader = RaylibShader.CreateSimpleShader(vs, fs, glsl_ver);

// in the loop, around the draws that should be lit:
Raylib.BeginShaderMode(shader);
    Terrain.Draw();
    CUBE.Draw();
Raylib.EndShaderMode();
```

The two shader pairs exist because GLSL 3.30 and 1.20 are not a version number
apart: 1.20 uses `attribute`/`varying`, `texture2D()` and `gl_FragColor`, none
of which 3.30 accepts. One file cannot speak both. `GetGLSLVersion()` returns
120 / 330 / 100 / 300, and you pick the variant to match what the context
really gave you. This matters on old Intel graphics (GMA X3100 and the like,
which top out at OpenGL 2.1): a hard-coded 330 shader does not compile there and
the scene draws nothing at all.

### `RaylibSkybox` — a sky drawn entirely in a fragment shader

```
#native <RaylibSkybox>

RaylibSKYBOX.Skybox sky = RaylibSKYBOX.CreateSimpleSkybox();
sky.DailyCycle = RaylibSKYBOX.night;
sky.cloudON = true;
sky.CloudSpeed = 0.05;

sky.Update();
sky.Draw();                          // no extra shader needed
```

The sky is a gradient with a sun or moon disc, banded clouds and stars, all
produced by one shader the module compiles at run time.

### `RaylibFOG` — distance fog

```
#native <RaylibFOG>

RaylibFOG.FOG fogy = RaylibFOG.CreateFog(Raylib.SKYBLUE, 0.25f, 10.0f, 45.0f, true, true);
// colour, density, start, end, enabled, height-fog
fogy.Mode = RaylibFOG.ExponentialSquared;
fogy.HeightFog = true;
fogy.Height = 30.0f;
fogy.Falloff = 0.5f;
```

The fog module draws nothing by itself. It writes uniforms into the terrain's
own shader, so it has to be called **inside** `BeginShaderMode` — call it
outside and there is no current program to write to, and nothing happens.

### `RaylibSimpleWater` — a water surface

```
#native <RaylibSimpleWater>

RaylibSimpleWater.Water water = RaylibSimpleWater.CreateSimpleWaterFromOBJ("water.obj");
water.WaveStrength = 0.2f;
water.WaveSpeed = 0.5f;
water.WaveScale = 0.6f;
water.Reflection = 1f;

water.Update();
// inside BeginMode3D / the shader:
water.Draw();
// after EndMode3D, still in the 2D pass:
water.DrawUnderwaterEffect();
```

The water is geometry only (surface + floor). Whether you are swimming, and the
thresholds, live in `RaylibFPS` — so the moment the screen says "you are
swimming" and the moment the physics agrees cannot drift apart.
`DrawUnderwaterEffect` is a 2D overlay that only appears when the camera is below
the surface; it has to be called **after** `EndMode3D`, or it would take the 3D
perspective and slide around as you turn.

### `RaylibRender` — internal resolution

```
#native <RaylibRender>

RaylibRender.FullScreenRenderResolution(320, 240);
```

The scene is drawn at 320×240 and stretched to the screen. The expensive part
(terrain, lighting, water, shading) runs at about a quarter of the pixels, and
printing it to the screen is one texture draw. **The aspect ratio is not
preserved**: 320×240 (4:3) stretched to a 16:9 laptop makes circles into
ellipses. That is a deliberate choice. If you want no distortion, pick an
internal resolution with the screen's aspect — for 16:9 use 480×270 or 384×216,
not 320×240.

---

# 2. Raygui

Raygui is the immediate-mode GUI on top of raylib. It ships its own `#native`,
and it works the same way as Raylib: `Raygui.` prefix, controls return a result
code, and out-parameters come back through `Last*` accessors.

```
#native <Raylib>
#native <Raygui>

Raylib.Rectangle bounds = { 10, 10, 200, 30 };
Raygui.GuiButton(bounds, "click me");

Raygui.GuiSlider(bounds, "left", "right", value, 0, 100);
float changed = Raygui.LastFloat();       // the value the user dragged to
```

The controls are the ones you would expect — `GuiButton`, `GuiLabel`,
`GuiCheckBox`, `GuiSlider`, `GuiProgressBar`, `GuiSpinner`, `GuiComboBox`,
`GuiPanel`, `GuiWindowBox`, `GuiGroupBox`, `GuiLine`, `GuiScrollPanel`,
`GuiToggle`, `GuiToggleGroup`, `GuiDropdownBox`, `GuiValueBox`, `GuiTextBox`,
`GuiListView`, `GuiTabBar`, `GuiMessageBox`, `GuiColorPicker` and the rest. There
are around 85 of them. Styling goes through `Raygui.GuiSetStyle`,
`GuiLoadStyle`, `GuiSetFont`, `GuiSetAlpha` and so on.

The out-parameter slots are `LastInt`, `LastFloat`, `LastBool`, `LastV2X/Y`,
`LastV3X/Y/Z`, `LastColor`, `LastRectX/Y/W/H`, `LastFontBaseSize/GlyphCount/GlyphPadding`,
`LastStringLen/LastStringByte`.

---

# 3. Lua + LuaRaylib + LuaRaygui

There is a real Lua 5.4 in here, with two C modules: `LuaRaylib` and
`LuaRaygui`. A project keeps its Lua in `scripts/main.lua`, and
`Embed.Run(type="lua")` launches it as `gcl -luarun scripts/main.lua`. You can
also run one by hand:

```
gcl -luarun scripts/main.lua
```

`LuaRaylib` sets a global table called `raylib`. Everything is a plain Lua call
and the values come back as real Lua values — booleans are booleans, strings are
strings, and some calls return more than one value.

```
local raylib = require("LuaRaylib")   -- or just use the raylib global

raylib.InitWindow(800, 600, "hello")
raylib.SetTargetFPS(60)

while not raylib.WindowShouldClose() do
    raylib.BeginDrawing()
    raylib.ClearBackground(raylib.Color(0, 0, 0, 255))
    raylib.DrawCircleV(400, 300, 40, raylib.RED)
    raylib.EndDrawing()
end

raylib.CloseWindow()
```

This is where LuaRaylib differs from the GCL module, and the differences are
worth knowing before you port anything:

**Colours are still packed integers.** `raylib.RED`, `raylib.WHITE` and the rest
are the same `R | G<<8 | B<<16 | A<<24` numbers, and `raylib.Color(r, g, b, a)`
builds one. `raylib.Fade(c, a)` and `raylib.ColorAlpha(c, a)` adjust one. The
`raylib.Color` constructor is here as well, so both routes work.

**`Rectangle` is userdata with fields.** It is not four loose numbers. Build it
with `raylib.Rectangle(x, y, w, h)` and read or write `.x`, `.y`, `.w`, `.h`. It
is passed as a single value to the drawing and raygui functions.

```
local box = raylib.Rectangle(10, 20, 100, 50)
box.x = 30
raylib.DrawRectangleRec(box, raylib.RED)
```

**`Vector2`, `Vector3`, `Camera2D` and `Camera3D` are tables.** A `Camera3D`
has `position`, `target` and `up` sub-tables (each with x/y/z), plus `fovy` and
`projection`. The camera functions read them field by field:

```
local cam = raylib.Camera3D()
cam.position.z = 10
raylib.BeginMode3D(cam)
```

**Calls return real values.** `raylib.GetMousePosition()` returns two numbers,
`raylib.IsKeyDown(...)` returns a boolean, `raylib.GetClipboardText()` returns a
string. There are no `Last*` accessors here; you do not need them.

The surface covers window and core, cursor, drawing modes, timing, shaders, the
file system, random, input (keyboard, mouse, gamepad, touch, gestures), 2D
shapes, textures and images, text and fonts, the basic 3D primitives and audio.
It is not the full thousand-member set the GCL module has, but it is the part
you actually draw a game with.

`LuaRaygui` gives you the same GUI as the GCL side, as a global `raygui` table.
It **shares** the `Rectangle` type with `LuaRaylib` (they agree on the userdata
metatable), so a rectangle you built for raylib goes straight into raygui:

```
raygui.GuiButton(raylib.Rectangle(10, 10, 120, 30), "press")
local v = raygui.GuiSlider(box, "min", "max", value, 0, 100)   -- returns the new value
```

Controls that take an out-parameter **return it**, so there is no `Last*` step:
`GuiSlider` returns the value, `GuiCheckBox` returns the checked state,
`GuiComboBox` returns the active index, `GuiTextBox` returns the edited string.

---

# 4. Python + PyRaylib + PyRaygui

There is also a real Python 3.14, with two extension modules: `raylib` and
`raygui`. A project keeps its Python in `scripts/main.py`, and
`Embed.Run(type="python")` launches it as `gcl -pyrun scripts/main.py`. By hand:

```
gcl -pyrun scripts/main.py
```

The module is imported the normal way and everything is a normal Python call.

```
import math
import raylib

raylib.InitWindow(800, 600, "hello")
raylib.SetTargetFPS(60)

while not raylib.WindowShouldClose():
    raylib.BeginDrawing()
    raylib.ClearBackground(raylib.Color(0, 0, 0, 255))
    raylib.DrawCircleV(400, 300, 40, raylib.RED)
    raylib.EndDrawing()

raylib.CloseWindow()
```

The mapping is the Pythonic one, and it is different from both the GCL and the
Lua sides — again, worth knowing before you port:

**`Rectangle`, `Vector2` and `Vector3` are tuples.** Not objects, not userdata.
`raylib.Rectangle(x, y, w, h)` gives you `(x, y, w, h)`, and the drawing
functions take that tuple directly.

```
box = raylib.Rectangle(10, 20, 100, 50)
raylib.DrawRectangleRec(box, raylib.RED)
```

**`Camera2D` and `Camera3D` are dicts.** Position, target and up are themselves
dicts with `x`/`y`/`z` (or `x`/`y`) keys:

```
cam = raylib.Camera3D()
cam["position"]["z"] = 10
raylib.BeginMode3D(cam)
```

**Colours are ints**, the same packed layout, and `raylib.Color(r, g, b, a)`
builds one. `raylib.Fade`, `raylib.ColorAlpha`, `raylib.ColorFromHSV` and the
rest return ints.

**Returns are Python values.** Booleans are `True`/`False`, strings are `str`,
tuples come back as tuples (`GetMousePosition` gives `(x, y)`, `GetWindowScaleDPI`
gives `(x, y)`).

**A few calls accept either shape.** `DrawCube` and `DrawCubeWires` take the
position as a `(x, y, z)` tuple or a dict followed by width/height/length/colour,
or as seven plain numbers. Both work.

`PyRaygui` mirrors `LuaRaygui`: a `raygui` module, tuple rectangles, and
controls that return their out-parameter rather than asking you to read a slot.

```
import raygui
raygui.GuiButton((10, 10, 120, 30), "press")
value = raygui.GuiSlider(box, "min", "max", value, 0, 100)
```

---

# 5. Sharing values between GCL, Lua and Python

The three runtimes are **separate processes**, so they cannot share memory the
usual way. They share values through a small memory-mapped store, and it is fast
enough to use inside a game loop — the pong demo writes the ball position every
frame and the other window reads it every frame, with no visible lag. (An
earlier version of this used a file that was read, rewritten and renamed on
every access; it stuttered and two processes could overwrite each other with
stale data. The current version is one memory read plus a very short spinlock.)

There are 64 slots. Each slot is a key and a value, and the value is a string —
so in practice it is numbers that round-trip cleanly.

From GCL:

```
#native <Embed>

Embed.Run(type="lua");                    // spawn gcl -luarun scripts/main.lua
Embed.Run(type="python");                 // spawn gcl -pyrun  scripts/main.py

Embed.SendValue(type="lua", name="score", value=42);
double s = Embed.GetValue(type="python", name="score");

Embed.IsActive(type="lua");               // 1 or 0
Embed.Stop(type="lua");                   // wait for it to exit
```

From Lua:

```
gcl.init()
gcl.set_state("paddle_y", paddle_y)
local ball_x = gcl.get_state("ball_x")    -- a number, or nil if unset
```

From Python:

```
import gcl
gcl.init()
gcl.set_state("paddle_y", float(paddle_y))
ball_x = gcl.get_state("ball_x")          # a float, or None if unset
```

The store's path comes from the `GCL_SHARED_FILE` environment variable, which
the parent sets for its children. `GetValue` returns a `double`, so a value that
is not a number is parsed with `atof` on the way back — stick to numbers.

One limit: **one process per runtime type.** Lua and Python can run together,
but not two Luas.

The full worked example is `examples/demo_pong_lua_vs_python/`: `main.gcsf` is a
GCL referee that owns a 1600×600 playfield and the ball; `scripts/main.lua` and
`scripts/main.py` each draw one half and run their own paddle. The ball really
does cross from one window into the other, because both are drawing the same
ball of the same playfield from two different origins.

---

# 6. The GCL language

The rest of this file is the language. Source files use the `.gcsf` extension.

GCL is C with a few things bolted on: `gcChar` strings that carry their own
length, sized types whose width is in the name (`int8`, `float32`), a `printf`
that uses `{}`, and native modules pulled in with `#native`. There is no
compiler backend — the lexer and parser build an AST and the runner walks it.
So it is an interpreter, and that one fact explains most of the quirks below:
every value lives in a `double` at run time, and the declared type only exists
to clamp the value on the way in. Keep that in mind and the overflow behaviour
stops being surprising.

## Running it

```
gcl -ide [file]                 open the IDE (the default with no arguments)
gcl -run file.gcsf [args...]    run a script; extra args land in main(argc, argv)
gcl -luarun file.lua            run a Lua script
gcl -pyrun  file.py             run a Python script
gcl -build project.gcdata [-o dir]   build a project into an executable
gcl -new path [--lua] [--luarun] [--python] [--pyraylib]   scaffold a project
```

Without `-debug` the runtime stays quiet — errors still go to stderr, the trace
output does not. If the executable finds a `.gcBundle` next to itself (the
output of `-build`), it runs the `main.gcsf` inside it instead of opening the
IDE.

## Comments

```
// C comment
/* multi
   line */

# GCL comment (to end of line)

#|
    GCL block comment
    |#
```

The `#| ... |#` form exists because the preprocessor treats any line starting
with `#` as a directive, so the block comment needed its own marker to stay out
of that path. It can be indented.

## Preprocessor

Lines beginning with `#` are handled before the lexer sees them.

| Directive | What it does |
| --- | --- |
| `#include <x.gcsf>` / `#include "x.gcsf"` | pastes the file in, through the same preprocessor — **only when the `#if` branch it sits in is active**; an unresolvable include is fatal (exit 1) |
| `#define NAME` / `#define NAME value` | object-like macro |
| `#define F(a,b) ...` | function-like macro, arguments substituted |
| `#undef NAME` | removes a macro |
| `#if` / `#ifdef` / `#ifndef` / `#elif` / `#else` / `#endif` | conditional compilation |
| `#warning ...` | yellow line on stderr; compilation continues |
| `#error ...` | red line on stderr and **compilation stops** (exit 1, nothing runs) |
| `#debug ...` | blue line on stderr; compilation continues |
| `#native <Name>` | adds a module to the load list |
| `#extern <file.so>` | loads an external library |
| `#register <ret> <fn>(<params>);` | declares an external function signature |
| `#pragma commandline` | puts the program into terminal-app mode |
| `#pragma once` | in an included file: include it at most once per run |

`#include` means "paste here", not "link", so including the same header from two
files embeds its contents twice — that is C's behaviour and it is deliberate.
The search order is: the directory of the file being run, then `<project>/include`,
then `<project>/lib`. For an include-once guard, put `#pragma once` in the
**included** file.

`#error` stops the build, exactly like C: no statement runs, stdout stays empty
even for a `printf` before the directive, and the process exits 1. A `#error`
inside a false branch never fires, which is the point of guarding it with `#if`.
`#warning` and `#debug` are diagnostics only.

`#native` collects a module name and loads it at start. The lookup order is:
`<dir of gcl>/Library/<Name>.(so|dll)`, then `<script dir>/Library/...`, then
`Library/...` in the working directory, then `PATH`. A module only has to export
one symbol, `gcl_<lowercase-name>_get_functions`, returning an array of
`GclNativeEntry`. Every native function has the signature
`double f(int argc, const char **argv)`, and arguments arrive as **strings** —
parse numbers yourself. `Stdio.writeFile(file="a", text="b")` reaches the module
as `"a"` then `"b"`; the `file=` labels are cosmetic.

`#extern` and `#register` are how you call plain C functions, and the support is
**limited**. Types are matched with a rough string rule (`char`→string,
`int`/`float`/`long`→int, `double`→double) and the call is hand-written for a
short list of shapes: return `void` with 0–2 parameters (with special cases for
`(int,int)` and `(int,int,char*)`), return `int` with 0 or 1 int parameter,
return `double` with 1 double parameter. Anything else returns 0. It exists to
wire up raylib quickly; if you are writing a real module, use `#native`.

```
#extern <raylib.so>
#register void InitWindow(int width, int height, const char *title);
```

`#pragma commandline` re-launches the program in the OS terminal, so a `scanf`
does not block forever under the IDE (which launches children without a
console). It also sets `GCL_TERMINAL=1`, so any Lua or Python started afterwards
opens its own window.

## Types

The C types you expect are here: `char`, `short`, `int`, `long`, `float`,
`double`, `long int`, `long long int`, `long double`, `unsigned int`. On top of
those, GCL adds sized types with the width in the name:

```
int8 int16 int32 int64 int128
uint8 uint16 uint32 uint64 uint128
float16 float32 float64 float128
bool void
```

These are a hard-coded word list in the lexer, so you cannot use them as
identifiers. Numeric literals are C-like — decimal, hex, fractional and
scientific all work, and the `l`/`L`, `u`/`U`, `f`/`F` suffixes come after the
exponent.

`gcChar` is a real UTF-8 string: not a pointer, a value that carries its own
contents.

```
gcChar text = "a rather long line of text";
```

### Overflow

Every value is a `double` at run time, clamped to the declared type on
assignment:

```
int8 a = 300;          // 44
uint8 b = -1;          // 255
bool c = 42;           // 1
float32 d = 1.0 / 3.0; // collapses to float precision
```

`int128`, `uint128` and `float128` are **not** really 128-bit. They are clamped
to `long long` and `double`. The "128" currently just means "the bigger one".
There is a special path for `uint64`/`uint128` variables and arrays that keeps
the original literal text alongside the number, because a `double` rounds large
unsigned values — that is why `uint64 m = 18446744073709551615;` prints
correctly even though it is past 2^53. The mirror is kept only when the literal
fits the type and is a plain run of decimal digits; put the value into
arithmetic and it rounds like any other double.

## Variables

```
int count = 10;
char c = 'A';
char *p = "text";
char buf[7] = "World";
gcChar s = "a longer string";
```

`const` is enforced **at run time**: assigning to it prints an error and
evaluates to 0.

`global` and `local` are the interesting ones:

```
int counter = 0;

int bump() {
    global counter;      // bind to the outer counter
    counter = counter + 1;
    return counter;
}

int scratch() {
    local base;          // owned by this call, freed on return
    base = 5;
    return base;
}
```

`global x;` inside a function means "bind to the `x` defined outside". If it does
not exist yet, it is created at 0 and kept after the call returns. Ordinary
locals are cleaned up on return. `inline`, `public` and `private` exist but are
no-ops — they are there so the language server recognises them, not as access
control.

## Operators

```
+  -  *  /  %
++  --
+=  -=  *=  /=
=  ==  !=  <  >  <=  >=
```

`=>` also lexes as `>=`.

Bit and boolean operators come in both symbolic and word form:

| Symbol | Word | Meaning |
| --- | --- | --- |
| `&&` | `AND` | logical and |
| `\|\|` | `OR` | logical or |
| `^` | `XOR` | bitwise xor |
| `~` | `NOT` | bitwise not |
| `!` | | logical not |
| `<<` | `LEFT_SHIFT` | shift left |
| `>>` | `RIGHT_SHIFT` | shift right |
| `&` | `BIT_AND` | bitwise and |
| `\|` | `BIT_OR` | bitwise or |

One caveat: `NOT x` flips bits (it resolves to `~`, not `!`). The unary `&` is
address-of, and its only real job is to flatten a struct's members when passing
it to a native module.

You can give operators names with `typedef`:

```
typedef && AND;
typedef ~  NOT;

number a = 10;
number b = 30;
number x = a XOR b;
```

After that the names behave like operators.

## Arrays and strings

```
int8 numbers[5] = {1, 2, 3, 4, 5};
char letters[5] = {'A', 'B', 'C', 'D', 'E'};
char greeting[14] = "Hello, World!";
char names[3][20] = { "Ali", "Veli", "Ayse" };
```

`char names[3][20]` is an array of strings — three slots of 20 characters.
`sizeof(numbers) / sizeof(numbers[0])` gives the element count, though **not for
the C reason**: `sizeof(array)` returns the element count and `sizeof(array[0])`
returns 1, so the end result matches C but the meaning is different.
`sizeof("text")` really is `strlen + 1`, and `sizeof(int)` returns a real byte
size. Indexing a string gives you the character code; assigning to an element
writes one char and does not truncate the string.

## struct, typedef, enum

```
struct Person {
    int id;
    int age;
    char name[20];
};

typedef struct {
    int id;
    int age;
    char name[20];
} User;
```

Structs can be built positionally (`{ 1, 25, "Ali" }`), by designation
(`{ .id = 2, .name = "Veli" }`), empty and filled later, copied from another
struct, or returned from a function. Arrays of structs and nested structs work.

When you hand a struct to a native function, its members unroll in declaration
order and are passed as arguments. That is the flattening rule from section 1,
and it is the only bridge between the two.

`enum` values start at 0 and increase; explicit values are not supported yet.

## Functions and control flow

```
void greet() { Stdio.printf("hello\n"); }
int add(int a, int b) { return a + b; }
```

Functions are collected before the body runs, so you can call one before it is
defined. `main` is called automatically at the end if present, and can take
`argc`/`argv`. Arguments are bound by value; string arguments are copied in.

```
if (a > b) { ... } else if (a == b) { ... } else { ... }
while (count < 10) { count++; }
for (int i = 0; i < n; i++) { ... }
switch (mode) { case 1: ... break; default: ... break; }
```

`break` and `continue` work. `switch` needs an explicit `break`, same as C.
There is a fail-safe against runaway loops: a `while` or `for` that runs more
than a million iterations aborts with "possible infinite loop". `for`
initializers are block-scoped.

## printf and scanf

Output uses `{}`, not `%`:

```
Stdio.printf("name: {} age: {} point: {.2f}\n", name, age, point);
```

`{}` prints the value; `{.Nf}` prints a float with N decimals; `\n`, `\t`, `\r`
are handled. Bare `printf(...)` and `scanf(...)` map to the Stdio module, so you
can drop the prefix.

`scanf` is a safe wrapper that writes into a variable:

```
int number = 0;
Stdio.scanf(number);       // reads a number
gcChar line = "";
Stdio.scanf(line);         // reads a line
```

It needs a real console, so a program that uses it under the IDE must carry
`#pragma commandline`.

## The standard modules

`Math` and `Stdio` are the two you use without a window.

```
#native <Math>
Math.randInt(0, 99);  Math.randFloat(0, 99);  Math.min(0, 99);  Math.max(0, 99);
Math.abs(-99);  Math.floor(9.99);  Math.ceil(9.01);  Math.round(9.5);
Math.sqrt(99);  Math.pow(2, 8);
Math.sin(90);  Math.cos(90);  Math.tan(90);       // degrees, not radians
Math.clamp(150, 0, 100);  Math.sign(-99);
Math.randBool();  Math.randChoice(1, 2, 3);
```

```
#native <Stdio>
Stdio.printf();  Stdio.scanf(variable);
Stdio.openFile(...);  Stdio.writeFile(...);  Stdio.appendFile(...);
Stdio.closeFile(...);  Stdio.fileExists(...);  Stdio.deleteFile(...);
Stdio.renameFile(...);  Stdio.fileSize(...);  Stdio.flushFile(...);
```

Both `openFile` and `openfile` spellings are registered. Note that `readFile`
returns the **size** of a file, not its contents; reading a file's text into a
variable is not implemented yet.

## Projects

`gcl -new <path>` scaffolds this layout:

```
ProjectName/
    assets/       icon.png (the default gnuchan logo, also the built exe's icon)
    external/     .so/.dll dropped here for #extern
    include/      .gcsf files for #include
    lib/          .gclib files
    out/          the executable and its .gcBundle after -build
    scripts/      main.lua, main.py
    main.gcsf
    project.gcdata
```

`project.gcdata` is JSON. Only `project_name` really drives the build (it
becomes the output name); `icon` is applied to the executable.

```
gcl -build project.gcdata [-o dir]
```

The build produces the executable and a `.gcBundle`, which packs the project
and the runtime `Library/` into one file (it works like a Godot PCK). The IDE
itself is not packed; only scripts and runtimes go in.

## Environment variables

| Variable | Purpose |
| --- | --- |
| `GCL_PROJECT_DIR` | project root; the Lua/Python children find their scripts through this |
| `GCL_SHARED_FILE` | path to the shared value store (section 5) |
| `GCL_TERMINAL` | set to `1` under `#pragma commandline`; children open their own terminals |
| `GCL_TERMINAL_CMD` | (Linux) override the terminal emulator used for spawning |
| `GCL_EXE_PATH` | path to `gcl`; the IDE uses it to invoke `-run`/`-new`/`-build` |

## Building GCL itself

The build chain is `src/makefile.py`.

```
cd src
python3 makefile.py            # deps + build + headless tests (ptest, ctest, gcsf)
python3 makefile.py raylib     # deps + build + the window/module tests (needs a display)
python3 makefile.py all        # both
python3 makefile.py install    # build and install so `gcl` is on PATH
```

It fetches Lua, a standalone CPython build and Raylib from source, compiles the
interpreter, the modules and the IDE, and runs the headless test layers. On
Linux it installs the X11 and OpenGL development packages Raylib's GLFW build
needs through the distribution's package manager before building Raylib, and it
prefers a user-local install under `~/.local` so a normal developer does not
need sudo. On install it also writes a `.desktop` entry so the IDE appears in
the launcher.

Build output lands under `_temp/language-build/<os>/`:

```
gcl
Library/       Math, Stdio, Embed, Raylib, Raygui and the wrappers
Library/Embeded/Lua_Runtime/     lua, LuaRaylib, LuaRaygui
Library/Embeded/Python_Runtime/  Python, PyRaylib, PyRaygui, gcl, and the stdlib
Programs/      ide
Assets/        FreeMono.ttf
```

`gcl` itself stays small. The IDE is a separate library (`Programs/ide`) and the
Lua and Python runtimes are loaded out of `Library/Embed`, so neither inflates
the CLI binary. The IDE is a raylib program; it opens its own raylib window,
which on this platform is an X11 window through GLFW.

One build detail worth writing down, because it looks like a bug when you first
see it: the sources really sit directly under `src/`, but the makefile still
refers to them as `_SRC/...` — a leftover from when this tree lived inside the
GnuchanOS repository. A single relative symlink, `src/_SRC -> .`, makes every
one of those paths resolve to the sources beside it. `GnuChanPrograms.sh` in the
GnuchanOS repository creates that link before building, and so does the CI
workflow; if you build by hand from a fresh clone, create it yourself:

```
ln -sfn . src/_SRC            # Linux
mklink /J src\_SRC src        # Windows (cmd), a junction needs no elevation
```

## Continuous integration

`.github/workflows/build.yml` builds the project on **Linux** and on
**Windows** on every push and pull request, and can be started by hand from the
Actions tab. It produces one artifact per platform: the whole output tree —
`gcl`, `Library/` with every module, `Programs/ide`, `Assets/`.

Two things in it are worth knowing, because they are the things that break a
build on a fresh machine:

- **The `src/_SRC` link above is made in the workflow**, per platform — a
  symlink on Linux, a junction on Windows.
- **Windows runs under the native Python**, not an MSYS one, because the
  makefile asks `platform.system()` what it is building for and only a native
  interpreter answers `Windows`. The MinGW toolchain (`gcc`, `mingw32-make`,
  `dlltool`, `gendef`) is put on the runner's PATH by hand, because
  `msys2/setup-msys2` keeps its `bin` inside the MSYS2 shell.

The workflow caches `_temp/<os>/` — the downloaded Lua, CPython, Raylib and
FreeFont — so a push that only touches a C source does not re-fetch all of it.
Bump the `-v1` suffix on the cache key to force a clean fetch.

The headless test layers (`ptest`, `ctest`, `gcsf`) read `language/tests/` and
`language/_SRC/`, which this repository does not carry, so the build reports
"test skipped" for each and carries on. The build itself — every module
compiling and linking — is the pass/fail signal in CI until the test data is
moved into this repository.

## Releasing

The version lives in **`version.txt`** at the repository root, and that file is
the only place it is written by hand. To publish:

1. Edit `version.txt` — one line, e.g. `0.2.0`.
2. Commit and push.

That is the whole procedure. On the push, both platforms build; when they both
pass, a `release` job reads `version.txt` and publishes
[github.com/gnuchanos/gcl/releases](https://github.com/gnuchanos/gcl/releases)
with the tag `v<version>` (so `version.txt` containing `0.2.0` publishes
`v0.2.0`), release notes generated from the commits, and two archives — the
full Linux tree as `gcl-linux-<version>.zip` and the Windows tree as
`gcl-windows-<version>.zip`.

**A push without a version bump publishes nothing.** The release job checks
whether `v<version>` already exists and stops if it does, so ordinary pushes
just build. Only an actual change to `version.txt` produces a release.

A few consequences of that rule, so they are not surprises:

- **The release runs only if both builds pass.** If Windows is red, nothing is
  published — a release with one platform in it would look like it covered both.
- **The version is not compared for ordering.** `0.10.0` and `0.9.0` are just
  different strings; going "back" to an older number is allowed if you type it.
- **There is no version string in the code yet.** `version.txt` is read by the
  release job alone. Baking it into the binary (a `--version` flag) is a
  separate change.

## Known limitations

A short honest list:

- **No real 128-bit types.** `int128`/`uint128`/`float128` clamp to 64-bit/`double`.
- **Everything is a `double` at run time.** Large integers lose precision once
  they enter arithmetic, even though the literals print correctly.
- **`#warning`/`#debug` never abort.** Only `#error` stops the build.
- **`#extern`/`#register` support only a few calling shapes.**
- **`readFile` returns a size, not contents.**
- **`enum` has no explicit values.**
- **`sizeof(array)` returns an element count, not bytes.**
- **`const` is checked at run time**, not compile time.
- **Undefined variables are a runtime error**, printed to stderr; the expression
  evaluates to 0 and the program keeps going rather than stopping.
- **One process per runtime type** in the Embed system — Lua and Python together
  is fine, two Luas is not.

## License

GPL3.
