# doomgeneric in RavenOS

The Doom engine RavenOS runs. It is [doomgeneric](https://github.com/ozkl/doomgeneric) at commit
`e257a09527062bfb986b6e5e0780839b6cbd5b71`, licensed under the GNU GPL version 2 (`COPYING`). doomgeneric is
id Software's Doom by way of Chocolate Doom, with the platform code reduced to a few functions a port provides.

The game data is Freedoom Phase 1 (`freedoom1.wad`, BSD-3-Clause), built into the Doom data disk by
`tools/build-wad.sh`. It is not part of this directory.

## What is here

- **Upstream engine files, kept as they are:** the sources upstream's `Makefile` builds (its `SRC_DOOM` list) except
  the X11 platform layer, and all of upstream's headers. `README.upstream.md` is upstream's README.
- **Not kept:** the other platform layers (SDL, X11, Allegro, Windows, the Linux framebuffer and others), sound and
  music back ends, build files, and screenshots.
- **RavenOS's own files**, under the same GPL version 2:
  - `doomgeneric_ravenos.c`: the platform layer. Video goes through the kernel's gfx session (640x400x32), keys
    through the key mapper, and time through the runtime clock.
  - `raven_doom_input.c` and `raven_doom_input.h`: the key mapper. It maps physical keys to Doom keys by scancode,
    merges left and right modifiers, and limits how many events each frame delivers. Doom's default controls are
    kept, and X also fires: macOS keeps Ctrl with the arrow keys for switching desktops.
  - The kernel side of the port lives outside this directory: `kernel/doom_bridge.c` and
    `kernel/raven_doom_bridge.h`.

## Changes to upstream files

Every change is under `#ifdef RAVENOS` and leaves the upstream code path as it was. Each changed file says so in
its first line. `tools/doomgeneric-diff.sh` prints the exact diff against the pinned commit, and fails if any other
upstream file differs.

| File | Change |
|---|---|
| `i_system.c` | `I_Quit` ends the Doom session (`raven_doom_exit(0)`) instead of the process. `I_Error` ends it with the error message (`raven_doom_abort`), keeping the message in a static buffer because the abort unwinds the stack. The Zenity dialog helpers, which shell out with `system()`, are left out. |
| `m_menu.c` | Load Game, Save Game, Quick Save and Quick Load show "saving and loading aren't available in ravenos" instead of opening the save slots. RavenOS gives Doom no writable storage. |
| `m_config.c`, `m_config.h` | No float config variables: the `DEFAULT_FLOAT` type, its two entries (`mouse_acceleration`, `libsamplerate_scale`), the float cases in loading and saving (`atof`, `%f`) and `M_GetFloatVariable`. No configuration files: no configuration directory (so no savegame directory either), and loading and saving the defaults return before any file I/O, so every variable keeps its built-in default. |
| `i_video.c`, `i_video.h` | No `mouse_acceleration`, a float only the config file and the mouse-speed box used. |
| `v_video.c` | No `<math.h>`, and `V_DrawMouseSpeedBox`, a `-testcontrols` diagnostic that works in floating point, is empty: RavenOS has no mouse. |
| `g_game.c` | No `<math.h>`, and the `-timedemo` report gives its tic counts without the frame rate. |
| `i_input.c`, `p_setup.c`, `r_main.c` | No `<math.h>`: nothing compiled in these files uses it. (`r_main.c`'s table generation, which does, is upstream's `#if 0` code.) |

RavenOS has no floating point: the engine is compiled with `-msoft-float -mno-sse -mno-mmx`, and
`tools/doom-audit.sh` proves no float helper, x87 instruction or float format string is left. `i_sound.c` still
declares `extern float libsamplerate_scale`, but only code under `FEATURE_SOUND`, which RavenOS doesn't define,
uses it.
