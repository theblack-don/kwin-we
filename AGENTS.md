# AGENTS.md — KWin (KineticWE Fork)

This file defines the sub-agentic workflow for the KWin compositor fork.
KWin is a Wayland (and X11) compositor written in C++23 with Qt6, using QML
for effects/scripts and JavaScript for window-management scripts.

> **References**
> - [KWin Scripting Tutorial](https://develop.kde.org/docs/plasma/kwin/)
> - [KWin Effects (QML) Tutorial](https://develop.kde.org/docs/plasma/kwineffect/)
> - [KWin API Docs](https://develop.kde.org/docs/plasma/kwin/api/)
> - [KDE Frameworks Coding Style](https://community.kde.org/Policies/Frameworks_Coding_Style)
> - Upstream: https://invent.kde.org/plasma/kwin

---

## Project Structure

| Path | Contents |
|------|----------|
| `src/` | Core C++ source, plugins, scripting, QML, backends |
| `src/kcms/` | **KCMs** (System Settings modules): animations, decoration, desktop, effects, options, rules, screenedges, scripts, tabbox, tiling, virtualkeyboard, xwayland |
| `src/plugins/` | **Effect plugins** (one per folder): blur, overview, tiling, screenshot, etc. `src/plugins/private/` exposes `WindowHeap` and layouting helpers for QML effects |
| `src/scripting/` | JavaScript / QML scripting engine bindings |
| `src/qml/` | QML components (frames, on-screen notification, outline) |
| `src/tabbox/` | Tab switcher logic and QML switchers in `src/tabbox/switchers/` |
| `src/tiling/` | Native tiling engine (this fork's main addition) |
| `src/backends/` | DRM, libinput, virtual, Wayland, X11 backends |
| `src/core/` | Color management, outputs, pipelines, DRM formats |
| `src/wayland/` | Wayland protocol implementations and tools |
| `src/xwayland/` | Xwayland integration |
| `autotests/` | Unit and integration tests |
| `examples/` | `quick-effect/` and `quick-script/` reference packages |
| `data/` | Desktop files, icons, DBus service files |
| `doc/` | `coding-conventions.md`, `TESTING.md` |

---

## Skills

### `kwin-core`

Use this skill when editing C++ source files in `src/` (excluding `src/kcms/`,
`src/scripting/`, `src/qml/`, and effect `main.qml` files).

**Scope:** Window management, compositor, backends, color pipeline,
Wayland/X11 protocols, tiling engine, tab switcher C++ side.

**Conventions:**
- C++23, Qt6.10+, KF6.26+
- Follow KDE Frameworks Coding Style (`doc/coding-conventions.md`)
- Commit style: `component/subcomponent: Do a thing`
- Prefer `auto` only when it avoids repetition or for iterators.
- **Avoid** `QRect::right()` / `QRect::bottom()` except when snapping or clipping matching borders (see `doc/coding-conventions.md`).
- Window-management logic lives in files ending with `client` (e.g. `x11client.cpp`, `xdgshellclient.cpp`).
- Use `.clang-format` at repo root.

**Key APIs / Patterns:**
- `workspace` global provides window-manager interface.
- `options` global provides read-only current configuration.
- Signals/slots and Q_PROPERTY expose scriptable properties.
- `KConfigXT` for structured settings.
- `KPackage` for packaging effects and scripts.

### `kwin-qml`

Use this skill when editing `.qml` files, especially in:
- `src/plugins/*/contents/ui/`
- `src/kcms/*/ui/`
- `src/qml/`
- `src/tabbox/switchers/`
- `examples/`

**Scope:** QML-based KWin effects, quick scripts, KCM UIs, tab switcher skins,
on-screen notifications.

**Conventions:**
- `import QtQuick` (Qt6 style — no version number)
- `import org.kde.kwin` for KWin-specific types: `SceneEffect`, `WindowThumbnail`, `WindowModel`, `ShortcutHandler`, `ScreenEdgeHandler`, `PinchGestureHandler`, `SwipeGestureHandler`, etc.
- `import org.kde.kirigami as Kirigami` for Kirigami units and controls.
- Every QML effect **must** have `SceneEffect` as root with a `delegate` property.
- `delegate` is instantiated **per screen**; use `SceneView.screen` to know which output.
- `effect.configuration` is a map of `KConfigXT` entries declared in `contents/config/main.xml`.
- Config UI widgets in `.ui` files must be named `kcfg_<KeyName>`.

**Packaging:**
- QML effects use `KPackageStructure: "KWin/Effect"` in `metadata.json`.
- Quick scripts use `KPackageStructure: "KWin/Script"` and `"X-Plasma-API": "declarativescript"`.
- Install via: `kpackagetool6 --type KWin/Effect --install package/`

### `kwin-javascript`

Use this skill when editing `.js` files in `src/scripts/`, `autotests/`, or user
script packages (not QML `.qml` files).

**Scope:** KWin window-management scripts, effect test scripts, automation.

**Conventions:**
- ECMAScript / JavaScript (not TypeScript).
- Global objects available: `workspace`, `options`, `readConfig(key, defaultValue)`, `print(...)`.
- Access all managed windows via `workspace.clientList()`.
- Listen to signals: `workspace.clientAdded`, `workspace.clientMaximizeSet`, `workspace.windowRemoved`, etc.
- A "Client" is a managed window; properties marked with Q_PROPERTY in C++ are scriptable.
- Config stored in `~/.config/kwinrc`; read with `readConfig()`.
- For user scripts, package structure:
  ```
  myscript/
  ├── contents/
  │   └── code/
  │       └── main.js
  └── metadata.json
  ```
- `metadata.json` must include `"X-Plasma-API": "javascript"`, `"X-Plasma-MainScript": "code/main.js"`, and `"KPackageStructure": "KWin/Script"`.
- Enable with `kwriteconfig6 --file kwinrc --group Plugins --key <Id>Enabled true` then `qdbus org.kde.KWin /KWin reconfigure`.

**Debugging:**
- Run in Desktop Console: `plasma-interactiveconsole --kwin`
- View logs: `journalctl -f QT_CATEGORY=js QT_CATEGORY=kwin_scripting`
- Ensure `kwin_*.debug=true` is set; use `kdebugsettings` → KWin Scripting → Full Debug.

---

## Agents

### `settings-agent`

**Role:** Maintains and evolves all KWin configuration surfaces (KCMs, effect
configs, script configs, and `metadata.json` packaging).

**Responsibilities:**
- Add or modify KCMs in `src/kcms/`.
- Update `metadata.json` files for effects and scripts.
- Create `contents/config/main.xml` (KConfigXT) and `contents/ui/config.ui` for configurable effects/scripts.
- Ensure config keys are wired correctly (`kcfg_` naming, `effect.configuration` in QML, `readConfig()` in JS).
- Update `kwinrc` schema documentation and `kconf_update` scripts if settings migrate.
- Validate that new settings have sensible defaults and are exposed in System Settings.

**When to invoke:**
- User asks to add a setting, toggle, color picker, shortcut, or KCM page.
- Any change to `metadata.json`, `main.xml`, or `config.ui`.
- Refactoring settings from hard-coded values to user-configurable ones.

**Success criteria:**
- `kpackagetool6 --type KWin/Effect --validate` passes (where applicable).
- Settings appear and persist in System Settings or `kwinrc`.
- No breaking changes to existing config keys without `kconf_update` migration.

---

### `core-logic-agent`

**Role:** Handles C++ core changes, compositor logic, window management,
backends, protocols, and the native tiling engine.

**Responsibilities:**
- Modify or add C++ source in `src/` (excluding KCM C++ that is purely UI).
- Work on window lifecycle: clients (`x11client`, `xdgshellclient`, etc.), surfaces, activation, stacking, geometry, and rules.
- Work on compositor: rendering, scenes (OpenGL / QPainter / Vulkan), color pipeline, outputs.
- Work on backends: DRM, libinput, virtual, Wayland, X11.
- Work on protocols: Wayland protocol implementations in `src/wayland/`.
- Work on tiling engine in `src/tiling/`.
- Expose new scriptable properties via Q_PROPERTY and signals.
- Ensure thread safety and performance; KWin is in the hot path of every frame.

**When to invoke:**
- User asks to change window behavior, tiling logic, rendering, input handling, protocol support, or C++ APIs.
- Any addition of new C++ classes, refactoring of core algorithms, or backend changes.

**Success criteria:**
- Code compiles with the existing CMake build (`cmake --build build`).
- No regressions in existing autotests.
- Follows `doc/coding-conventions.md` and `.clang-format`.
- New public APIs are documented and, where applicable, exposed to scripting.

---

### `qa-agent`

**Role:** Ensures the codebase builds, tests pass, and nothing is broken after
changes. Acts as a gatekeeper.

**Responsibilities:**
- Run the build and report compile errors, warnings, or new deprecations.
- Run the test suite: `dbus-run-session xvfb-run ctest` from the build dir.
- Run individual tests in `build/bin/` with `dbus-run-session ./testFoo`.
- Check for formatting regressions (`clang-format`).
- Verify that changed files do not violate KWin coding conventions.
- Confirm that QML/JS packages validate with `kpackagetool6`.
- For substantial changes, insist on new unit/integration tests in `autotests/`.
- Monitor for ABI/API breaks in public headers.

**When to invoke:**
- After **every** non-trivial edit by any other agent.
- Before concluding any task.
- When the user explicitly asks "does it still work?" or "run tests."

**Success criteria:**
- `cmake --build build` succeeds with no new warnings treated as errors.
- `ctest` (or `xvfb-run ctest`) passes; failing tests are documented if expected.
- `git diff` shows no unintended changes.
- New tests are added for new features or bug fixes (substantial code).

**Build & Test Quick Reference:**
```bash
# Build
mkdir -p build && cd build
cmake ..
cmake --build .

# Run tests in isolated session
cd build
dbus-run-session xvfb-run ctest

# Run a single test
cd build/bin
dbus-run-session ./testFoo

# Run from build dir (nested Wayland session)
source build/prefix.sh
cd build/bin
env QT_PLUGIN_PATH="$(pwd):$QT_PLUGIN_PATH" \
  dbus-run-session ./kwin_wayland --xwayland konsole
```

---

## Workflow Rules

1. **Always start with the right skill.** If a task touches QML, load `kwin-qml`.
   If it touches JS scripts, load `kwin-javascript`. For everything else C++,
   load `kwin-core`.
2. **Delegate by agent domain.**
   - Settings / config / UI → `settings-agent`
   - Window management, rendering, protocols, tiling → `core-logic-agent`
   - Build, tests, validation, regression checks → `qa-agent`
   - Heavy graphics/scene work → `gfx-agent`
   - Input devices, gestures, accessibility → `input-agent`
   - Effect plugins lifecycle and shared resources → `effects-agent`
   - Native tiling engine rules and layouts → `tiling-agent`
3. **qa-agent gates every change.** After any other agent finishes, `qa-agent`
   must run build + relevant tests before the task is considered complete.
4. **Agents may ask for clarification** if a change spans multiple domains
   (e.g., adding a new tiling setting requires both a KCM and tiling-engine logic).
   In that case, the primary agent drafts the plan and delegates sub-tasks.
5. **Keep changes minimal.** Follow the existing style; do not refactor unrelated
   code.
6. **Document breaking changes.** If an agent changes a config key, script API,
   or C++ ABI, note it in the task summary so release notes can be updated.
7. **Fix Build Errors and Warnings** If There is a build error or warning when running the build or cmake commands we need to fix crucial errors and suppress not crucial ones. 

---

## Optional Agents (invoke when domain complexity warrants)

The following agents are **not** required for every task, but should be used
instead of `core-logic-agent` when the work is deep in their specific domain.

### `gfx-agent`

**Role:** Owns the graphics stack: scenes, shaders, color management, and GPU-specific rendering paths.

**Responsibilities:**
- Modify code in `src/scene/`, `src/opengl/`, `src/vulkan/`, `src/qpainter/`, and `src/core/color*`.
- Write or update GLSL / SPIR-V shaders and their C++ uniform/sampler wiring.
- Tune performance for specific GPU architectures (Mesa llvmpipe, NVIDIA, Intel, AMD).
- Handle color pipeline stages (LUTs, transformations, DRM formats).
- Investigate visual regressions: flicker, tearing, black screens, incorrect blending.

**When to invoke:**
- Any change to `GLShader`, `Scene`, `OpenGLBackend`, `VulkanBackend`, or QPainter scene.
- Adding new visual effects that require custom shaders.
- Debugging rendering artifacts or performance drops in the compositor.

**Success criteria:**
- Shaders compile on all supported backends (OpenGL, Vulkan, QPainter fallback).
- No visual regressions in `autotests/drm/` or effect integration tests.
- Color-managed outputs still pass ICC profile verification.

---

### `input-agent`

**Role:** Owns input devices, event routing, gestures, keyboard layouts, and accessibility input features.

**Responsibilities:**
- Modify code in `src/backends/libinput/`, `src/input*`, and gesture handlers.
- Add or update touchpad, mouse, tablet, stylus, and game-controller support.
- Work on keyboard layout switching, key repeat, and modifier handling.
- Implement new swipe/pinch gestures or screen-edge behaviors.
- Ensure accessibility features (mouse keys, sticky keys, bounce keys) keep working.

**When to invoke:**
- Any change to libinput event processing, input redirects, or gesture recognition.
- Adding new global shortcuts or gesture bindings.
- Debugging input lag, lost events, or incorrect pointer mapping on multi-monitor setups.

**Success criteria:**
- Input events are routed correctly in nested (`kwin_wayland --xwayland`) and DRM sessions.
- Existing gesture tests in `autotests/` still pass.
- No regressions in accessibility input plugins (`bouncekeys`, `mousekeys`, `stickykeys`, etc.).

---

### `effects-agent`

**Role:** Owns the effect plugin ecosystem in `src/plugins/*` and the shared resources they consume.

**Responsibilities:**
- Add, remove, or refactor effect plugins (e.g. blur, overview, slidingpopups).
- Manage `X-KDE-Ordering` in `metadata.json` to prevent effect conflicts.
- Maintain `src/plugins/private/` helpers (`WindowHeap`, layouting utils) used by QML effects.
- Audit effect resource usage (offscreen textures, FBOs, window thumbnails) for leaks or excess memory.
- Coordinate with `settings-agent` when an effect needs new configuration UI.

**When to invoke:**
- Any change inside `src/plugins/` that is **not** purely QML or config UI.
- Adjusting effect load order or dependencies.
- Adding shared helpers for multiple QML effects to use.

**Success criteria:**
- All affected effects still load and unload cleanly.
- `kpackagetool6 --type KWin/Effect --validate` passes for any changed package.
- Effect integration tests (`autotests/integration/effects/`) pass.

---

### `tiling-agent`

**Role:** Deep expertise in the native tiling engine (`src/tiling/`) and its integration with window management.

**Responsibilities:**
- Implement or modify tiling layouts (tile trees, gaps, padding, constraints).
- Wire tiling rules to KCM pages in `src/kcms/tiling/`.
- Ensure tiling interacts correctly with window rules, maximization, and fullscreen.
- Handle multi-output tiling (per-monitor layouts, moving tiles across outputs).
- Expose tiling state to scripting (`workspace.tiling` or similar) where appropriate.

**When to invoke:**
- Any change in `src/tiling/` or `src/kcms/tiling/`.
- Adding new tiling policies (e.g. fibonacci, master-stack, dwindle).
- Debugging geometry fights between tiled and floating windows.

**Success criteria:**
- Tiling autotests pass (or new ones are added for new layouts).
- Existing floating windows are never forced into tiles without explicit user action.
- KCM settings apply live without requiring a KWin restart.

---

## Optional Skills (load when task matches)

### `kwin-shader`

Use this skill when writing or editing shader code (GLSL / SPIR-V) or the C++
that drives it.

**Scope:** Visual effects, color transformations, post-processing, and any
compositor stage that runs on the GPU.

**Conventions:**
- GLSL version must match the OpenGL context KWin targets (check existing shaders).
- Prefer `KWin::GLShader` and its uniform helpers over raw OpenGL calls.
- Ensure shaders declare `precision mediump float;` or appropriate precision when used with OpenGL ES paths.
- Use `QMatrix4x4` for transform matrices passed as uniforms.
- For Vulkan, use the existing SPIR-V pipeline helpers in `src/vulkan/`.

**Key APIs / Patterns:**
- `Scene::createShader()` and `ShaderManager` for shader lifecycle.
- `GLFramebuffer` for offscreen passes.
- Color pipeline stages in `src/core/colorpipelinestage.cpp` for HDR/ICC-aware transforms.

---

### `kwin-wayland-protocol`

Use this skill when adding or modifying Wayland protocol XMLs or their C++
implementations.

**Scope:** `src/wayland/`, `src/wayland/protocols/`, and generated binding code.

**Conventions:**
- Place upstream-stable protocol XMLs in `src/wayland/protocols/`.
- Use `wayland-scanner` (invoked by CMake) to generate headers; never edit generated files by hand.
- Implement protocols by subclassing `QWaylandCompositorExtension` or KWin's own wrapper classes.
- Keep thread safety in mind: many Wayland callbacks run on the compositor thread.

**Key APIs / Patterns:**
- `KWin::WaylandServer` as the central dispatcher.
- `Display::createSurface()` and `SurfaceInterface` for surface lifecycle.
- Register new globals with `Display::addOutput()` patterns.

---

### `kwin-x11`

Use this skill when editing X11-specific client code, Xwayland integration, or
legacy window management paths.

**Scope:** `src/xwayland/`, `x11client.cpp`, ICCCM/EWMH atoms, and X11 backend code.

**Conventions:**
- Prefer modern Wayland paths where possible; X11 code is maintenance-only for new features unless explicitly requested.
- Atom names must match ICCCM / EWMH specs exactly (`_NET_WM_WINDOW_TYPE`, `_NET_ACTIVE_WINDOW`, etc.).
- Use `KWin::X11Client` for managed Xwayland windows; raw X11 windows are handled in `src/xwayland/lib/`.
- Keep X11 code behind `#if KWIN_BUILD_X11` where appropriate.

**Key APIs / Patterns:**
- `Atoms` singleton (`src/atoms.h`) for cached X11 atoms.
- `XwaylandInterface` for bridging Wayland and X11 window state.
- `NETRootInfo` and `NETWinInfo` from KWindowSystem for EWMH interactions.

---

## Memories

The following memories are suggested for persistent context across sessions.
They should be updated by the relevant agent whenever preferences or
environment facts change.

### `tiling-layout-preferences`

**Maintained by:** `tiling-agent`

**Contents:**
- Default tiling layout per output (e.g. "bsp", "monocle", "dwindle").
- Gap sizes (inner, outer) and whether gaps are configurable per-desktop.
- Rules for which window classes should start tiled vs floating.
- Keyboard shortcuts reserved for tiling operations.

**Usage:** Enables `tiling-agent` to make layout suggestions and avoid
re-asking the user for basic preferences.

---

### `effect-combinations`

**Maintained by:** `effects-agent`

**Contents:**
- Known-working effect stacks (e.g. blur + slidingpopups + overview).
- Known-broken combinations (e.g. two effects both grabbing the same screen edge).
- `X-KDE-Ordering` overrides that have been applied to resolve conflicts.
- Memory/performance notes for heavy effects (e.g. "blur + videowall = high VRAM").

**Usage:** Helps `effects-agent` quickly reject or warn about incompatible
effect combinations before they reach `qa-agent`.

---

### `hw-config`

**Maintained by:** `gfx-agent` and `qa-agent`

**Contents:**
- Primary GPU vendor and driver (Mesa/NVIDIA/Intel/AMD).
- Multi-monitor setup details (outputs, scales, HDR capability).
- Known driver bugs or workarounds (e.g. "NVIDIA requires explicit sync",
  "Intel Xe needs specific shader precision").
- Whether the test environment uses llvmpipe or a real GPU.

**Usage:** Allows `gfx-agent` to tailor shader precision, backend selection,
and feature toggles without probing the user every time.
