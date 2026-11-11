# THC: instructions for agents

THC is a lightweight multimedia player written in **C** (C11). **Stay in C.** See `README.md`: it is the spec.

## Goal for v0.1.0
A demo that shows off **everything in the README**, close to the final design. We optimize afterward. Features the README marks *(planned)* or *(later)* (audio effect plugins, screen reader support, episode metadata fetching) may be stubs.

If you deviate from the README, update the README in the same change.

## Build order (finish and commit each slice before the next)
1. SDL3 window + libmpv playback of one file
2. SQLite resume + watched tracking (playback bar and checkmark)
3. First-launch folder setup + music and video portals
4. Show -> season -> episode parsing and library sections (Anime, Television, Cartoons, Movies)
5. Music and audiobook playback (lib-Peep for FLAC/WAV; OGG, MP3, M4B)
6. Settings (themes, fonts, hotkeys, watched threshold, accessibility) and THC's separate MPV config

## Hard rules
- **Never write to, move, rename, or reorganize users' media files.** Sorting only changes presentation inside THC.
- **Privacy:** no network requests, no telemetry, no accounts. Data stays local.
- **THC's MPV config is separate** from any standalone MPV install: set libmpv's `config=yes` and `config-dir` to THC's own directory. Never read or write the system MPV config.
- All MPV options must remain usable (mpv.conf, input.conf, scripts, shaders, profiles).
- Watch history lives in SQLite in the OS standard data directory, keyed by relative library path + file size + duration.
- Must run on Windows, Linux (Wayland and X11), and macOS. No display-server-specific code.
- Dependencies must be compatible with the Unlicense; prefer public domain or permissive libraries. Do not add GPL code to THC itself.

## Workflow
- Work on `dev`. **Never push directly to `main`.**
- Versioning: `x.0.0` major, `0.x.0` minor (big features), `0.0.x` patch (small features, fixes, security).
- Build: `cmake -S . -B build && cmake --build build && ctest --test-dir build`
- Add tests for the history database and the folder parser. CI builds on all three OSes; keep it green.
- Layout: `src/` sources, `include/` headers, `tests/` tests.

## Resources
- lib-Peep: https://github.com/Doggydoggywhatnow/lib-Peep

## Getting started
Read `README.md` and this file, then build v0.1.0 on `dev` following the slice order above. Commit and push after each slice.

## CI
CI (`.github/workflows/ci.yml`) currently only builds the skeleton. When you add SDL3, libmpv, SQLite, or lib-Peep, update the workflow in the same slice to install them on all three OSes (Linux, Windows, macOS) so CI stays green.

## lib-Peep
Before using lib-Peep, read its README and source at https://github.com/Doggydoggywhatnow/lib-Peep to learn its API. If the interface is unclear or undocumented, note what you assumed in your commit message and in the README.

## UI design
No mockups exist. Design the UI yourself to match the README's values: visually pleasant, with a streaming-service feel. Give the music and video portals each a distinct look, and show the playback bar and checkmark under each video. Support light and dark mode, and design for accessibility from the start (keyboard navigation, scalable text, high contrast). Keep the design consistent and document major UI decisions in the README.

## Licensing checks
- Prefer an LGPL build of libmpv (`-Dgpl=false`) or launching mpv as a separate process. If you link a GPL libmpv, say so in the README's License section.
- Before adding any OGG or MP3 decoder, confirm it is public domain or permissively licensed (for example libvorbis, minimp3, dr_mp3, or stb_vorbis) and record the choice and its license in the README.
- Never add GPL code to THC's own source.
