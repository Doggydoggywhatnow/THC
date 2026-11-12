# Changelog

THC uses `x.y.z` versions: major, minor (big features), patch (small features, fixes, security).

## 0.3.0

### Added
- **NSFW portal** (#13), off by default and fully local. Mark files, episodes, movies, songs, albums, books, shows, seasons or any folder as NSFW (right-click menu, `N`, Undo); a folder mark covers files added later, a library folder can count as NSFW, and optional name rules mark more, never over an explicit "Not NSFW". Marked items leave the normal portals and show only in the NSFW tab (Video, Music, Audiobooks, Screenshots) with its own look and a beer-bottle XXX icon. The tab hides and shows in one action (`Ctrl+Shift+H`, its right-click menu, or Settings). Optional PIN lock (salted, iterated PBKDF2-HMAC-SHA-256; a courtesy lock, not security) with auto-relock. **Panic key** (`Ctrl+Shift+X`, also inside the MPV window) stops playback, closes the window, locks the portal and clears NSFW items from every queue. Nothing leaks: no history, plays or backlog; fixed window title, OSD and screenshot names; media keys and now-playing say only "Playing"; masked queue and bar; thumbnails and screenshots in their own folders, removed at exit; `thc-inspect` never prints NSFW names. History database schema 3 (a `nsfw_tags` table; older THC versions keep working).
- **Screenshots** (#25): MPV's `S` key saves into THC's own `screenshots/` folder, and a new Screenshots tab shows them newest first (open the folder, copy the path, delete after confirming). Folder, format (PNG default, JPG, WebP) and name template are settings; THC controls `screenshot-directory`, `screenshot-format` and `screenshot-template`.
- **Back up and restore** (#26): Settings, Privacy copies your settings and MPV setup (and, if you choose, history and screenshots) into a plain folder; restoring asks first, saves your current files into `pre-restore/` and applies the backup at the next start.
- **Settings across releases** (#26): settings an older THC does not know are kept in `settings.ini` and written back unchanged, so a downgrade no longer drops them. The data folder is the same for every package (the Snap is documented as the exception).
- **Performance baseline** (#12): the `THC_PERF` timing hook, `tools/perf` (synthetic libraries up to 50,000 files) and `docs/performance.md` with measured numbers and targets; a manual/nightly CI job runs the 1,000-file case.

### Fixed
- **History no longer takes another file's entry** (#24): a same-size file takes over a row only when the old path is really gone from the library, including for plays through the queue.
- **Album order with untagged files** (#20): tagged tracks by disc and track, untagged ones after in file-name order, one order for the album list, the queue and Play; untagged rows show a dash instead of a made-up number.
- **Video thumbnails no longer collide** (#22): the cache key includes the first and last 4 KB of the file, read once per session.
- Tests no longer put the 283 KB settings on the stack (a Windows crash found by CI).

### Not in this release
- #6 (accessibility shaders) was the optional last slice and is left out; it stays open. Large cover images (#21) are planned for a later release. "Why is this NSFW" in the NSFW rows (#28). Relocking when the screen locks (SDL gives no event). The known gaps from 0.2.0 (see its entry).

## 0.2.0

### Added
- **Queue** with a panel, drag and keyboard reordering, shuffle, repeat, backlog, M3U8 export and import, right-click menus and Add buttons. Video queues chain in one window. (#1, #11)
- **Music search** with typo tolerance. (#7)
- **Listening history:** plays, Recently played, Most played, per-portal history switches, Private session, clear per kind, JSON and CSV export. History database schema 2 (older databases are upgraded after a backup). (#11)
- **Media keys and now playing** (MPRIS on Linux; focused-window keys elsewhere), with video private by default. (#23)
- **Keep the video window on top**, with a hotkey. (#18)
- **thc-inspect** and the **Block network in MPV** switch, plus a "what THC stores" list. (#14)

### Known gaps
- System-wide media keys and the lock-screen widget on Windows and macOS; connection listing in thc-inspect outside Linux; always-on-top still needs a real-desktop check. See `docs/0.2.0.md`.

## 0.1.3

### Fixed
- **History now works across versions.** The history database records a schema version that is only ever raised. A newer THC upgrades an older database (after saving a `history.db.v<N>.bak` copy next to it) and keeps every row; an older THC that meets a newer database leaves its version and extra data alone and still reads and writes the entries it knows. Before, every start reset the version marker. New tests cover 0.1.x databases, unversioned ones, forward migration, newer databases and failed upgrades.

## 0.1.2

### Added
- Settings → MPV shows where THC's MPV files are and lets you copy each path (folder, `mpv.conf`, `input.conf`, `script-opts/osc.conf`, shaders). (#17)
- On-screen controller support: a **Create** button and a layout picker (bottom bar, top bar, box, slim box) for `script-opts/osc.conf`. (#17)
- GLSL shader support is explained in Settings, with a list of the shaders in THC's `shaders/` folder and buttons to copy a shader's path or its `mpv.conf` line. Every row shows its full path, and the `scripts` folder has Copy path and Create buttons too. (#17)
- **Config check:** THC asks MPV about every line of `mpv.conf` and lists the ones MPV skips, with line numbers. Controller settings in the wrong file (`layout=topbar` in `mpv.conf`) are called out with the fix, and options THC controls are marked as ignored. Settings also show the libmpv in use and its API version. (#17, #16)

### Fixed
- Scripts in THC's MPV `scripts/` folder never ran, because libmpv does not autoload them. THC now loads them. (#16)
- A new test checks that edits to `mpv.conf`, `input.conf`, profiles and scripts change MPV, and that a standalone MPV config is never read. (#16)


### Checked
- Resume always follows THC's history (#5) and the logo opens About and Settings on one page (#10); both shipped in 0.1.1 and were re-checked.

## 0.1.1

### Added
- The THC logo opens About and Settings on one scrolling page, with the version number. The gear button is gone. (#10)
- Tags from audio files: titles, artists, albums, track and disc numbers, year, genre and embedded cover art for MP3, FLAC, OGG, M4B and WAV, plus narrator and chapter count for audiobooks. Albums are ordered by track number. (#8)
- Video thumbnails: a picture next to the video, or a frame grabbed from it, shown on episodes and posters. Can be turned off or cleared in Settings. (#9)

### Fixed
- THC sometimes resumed from an older position because MPV's own resume competed with THC's history. THC's history is now the only source, whatever `mpv.conf` says. (#5)

## 0.1.0

- First demo: video, music and audiobook portals, resume and watched tracking, library sections, THC's own MPV configuration, settings, hotkeys and accessibility options.
