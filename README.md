# THC

**A lightweight multimedia player for the music and videos you already have.**

THC is a graphical interface for your local media, written in C. It shows what's already on your device, built around the way you already organize your files, and makes browsing it feel as unique and enjoyable as booting up a streaming service, without the streaming service. No accounts, no uploads, no subscriptions: just your library, on your device.

> **Status:** version 0.2.0, an early demo of the full design. Sections marked *(planned)* or *(later)* describe where THC is headed rather than what ships today.

---

## Table of contents

- [Why THC?](#why-thc)
- [About the name](#about-the-name)
- [Values](#values)
- [Features](#features)
- [First launch](#first-launch)
- [Library sections](#library-sections)
- [Watch history](#watch-history)
- [Audio and audiobooks](#audio-and-audiobooks)
- [Supported formats](#supported-formats)
- [Platforms](#platforms)
- [How it works](#how-it-works)
- [Interface design](#interface-design)
- [Watch tracking and MPV configuration](#watch-tracking-and-mpv-configuration)
- [Downloads](#downloads)
- [Building from source](#building-from-source)
- [Usage](#usage)
- [Configuration](#configuration)
- [Privacy](#privacy)
- [Accessibility](#accessibility)
- [Roadmap](#roadmap)
- [FAQ](#faq)
- [Contributing](#contributing)
- [Acknowledgments](#acknowledgments)
- [Versioning](#versioning)
- [License](#license)

---

## Why THC?

Streaming services made browsing media feel good: you see what's next, pick up where you left off, and know what you've already watched. Local files rarely get that treatment. They live in folders, behind file managers, with no memory of you.

THC brings that experience to the files you own. It plays your music and your videos, remembers where you stopped, and keeps track of what you've watched, all without anything leaving your machine.

## About the name

THC is a nod to getting high and watching movies, listening to music, and just enjoying your media. It's also a nod to the comfort of cannabis: in a similar way, this program is meant to be comfortable and reliable to use. And, well, smoking and watching TV is great.

## Values

1. **Visually pleasant.** Browsing and playback should look good and feel good.
2. **Useful.** On your device we save your last recorded spot in a video file and record whether or not you have watched the file, making keeping up with shows a breeze.
3. **Accessible.** The amazing MPV has a great host of accessibility features, and we also allow the user to change many things.
4. **Private.** All on your device. What you watch is your business. Period.

The optional **NSFW portal** (below) goes further: anything you mark NSFW stays out of the normal portals, the system, caches and logs.

## Features

- A graphical interface for the media you already have
- Works from your existing folder structure, with the option to change how things are sorted
- Separate, dedicated **music** and **video** libraries, each with its own look
- Optional library sections: Anime, Television, Cartoons, and Movies
- **Resume playback:** remembers your last position in each video file
- **Watched tracking:** a playback bar and checkmark under each video
- **Video thumbnails:** a picture next to the video, or a frame THC grabs from it, on episodes and posters
- **Tags from your audio files:** titles, artists, albums, track numbers and embedded cover art
- **Queue, music search and listening history** (0.2.0), **screenshots** in THC's folder, and an optional, private **NSFW portal** (0.3.0)
- Audio effect plugin support *(later release)*
- Audiobook support with features suited to listening to books
- Its own MPV configuration, separate from any standalone MPV install, with all of MPV's config options available
- Runs on Windows, Linux, and macOS, on Wayland or X11
- Lightweight, written in C
- Fully local: no network features, accounts, or telemetry

## First launch

On first startup, THC lets you choose, or skip:

- a **music folder**
- a **video folder**

Pick each with your system's folder picker, or type the path. Each folder gets its own "portal," a distinct design for displaying its files. You can set up one, both, or neither and add more later in **Settings → Library**, where you can also add an **audiobooks folder** and a folder for each library section.

## Library sections

THC reads your files as they're already sorted, but you can change that sorting from within THC. Beyond your main folders, you can add sections later. The available sections are:

- **Anime**
- **Television** (live action)
- **Cartoons**
- **Movies**

A section gets its content in one of three ways:

1. A folder named after it inside your video folder (`Anime`, `TV`, `TV Shows`, `Television`, `Series`, `Cartoons`, `Animation`, `Movies`, `Films`, in any letter case).
2. A separate folder chosen for that section in **Settings → Library**.
3. Automatic detection for everything else in the video folder: folders that look like shows go to Television, single files and movie folders go to Movies.

### Folder structure

For shows (Anime, Television, Cartoons), THC expects your files to be organized as:

```
Show Name/
├── Season 1/
│   ├── Episode 1.mkv
│   └── Episode 2.mkv
└── Season 2/
    └── ...
```

That is, **show → seasons → episodes**. THC relies on this layout to group episodes, track progress per season, and show what you've watched.

THC is forgiving about names:

- **Seasons:** `Season 2`, `S02`, `Series 2`, `Book 1 Water`, `Specials` (shown last), or a plain number.
- **Episodes:** `S01E02`, `S01E01-E02`, `1x02`, `Episode 2`, `Ep. 2`, `E02`, anime-style `Show - 02 [1080p]`, or a leading number (`02 - Title`). Episodes are ordered by number, so episode 10 comes after episode 9.
- **Titles** are cleaned up for display: `Blade.Runner.2049.2017.1080p.BluRay.mkv` is shown as *Blade Runner 2049 (2017)*.
- Episodes placed straight in the show folder are grouped by the season in their names.

### Tags in your audio files

Music and audiobooks keep their folder structure, but THC also reads the **tags inside the files**, so things look right even when file names are messy:

- **Titles, artists, albums, track and disc numbers, year and genre**, plus the **cover art** embedded in the file. Albums are ordered by disc and track number when every file has one, otherwise by file name.
- **Audiobooks:** title, author, narrator, cover and the number of M4B chapters.
- **Formats:** MP3 (ID3v1, v2.2, v2.3, v2.4), FLAC and OGG (Vorbis comments and pictures), M4B/M4A (iTunes tags and covers), WAV (RIFF INFO and embedded ID3).
- **Order of preference:** a tag wins over a folder or file name; an embedded cover wins over a cover image in the folder.
- Tags are read in the background after each scan, so browsing never waits on your disk. Reading is strictly read-only: THC never writes or "fixes" tags, and it never looks anything up online.

### Video thumbnails

Videos get a thumbnail from, in order:

1. a **picture next to the video**: `Name.jpg`, `Name-thumb.jpg`, `Name-poster.png`, `Name-landscape.jpg` or `Name-fanart.jpg`;
2. a **frame grabbed from the video itself** by a hidden MPV (no window, no sound), taken about 10% in and skipping black frames;
3. the card's normal colorful placeholder.

Generated thumbnails are cached in THC's own `thumbs/` folder (never next to your media), made in the background so scrolling stays smooth, and the cache stays under about 500 MB. A file's thumbnail is kept when you rename or move it and rebuilt when the file changes. Turn them off, or clear the cache, in **Settings → Library → Thumbnails**.

### Movies and music

- **Movies:** one file or one folder per movie. In a movie folder the largest video is the film; extras are left out.
- **Music:** artist → album → track. Loose songs in the music folder are listed too.
- **Audiobooks:** author → book → files (`Author/Book/01.mp3`), one file per book (`Author/Book.m4b`), or a book folder without an author.

### Changing how things are sorted

Sorting only changes what you see in THC:

- **Sort order:** folder order, A to Z, recently added, or recently watched.
- **Move a show to another section** from its page (for example, a cartoon that was detected as Television). THC remembers this in its settings file; your folders stay exactly as they are.
- **Browse folders** on the video home page shows your folders exactly as they are on disk.

## Screenshots (0.3.0)

Press `S` in the MPV window and the picture is saved in THC's own `screenshots/` folder, never next to your videos. The **Screenshots** tab (hotkey `4`, remappable) shows them newest first with the video title and time; select one to see it larger, open the folder, copy its path or delete it (after a confirmation, and only files inside that folder can be deleted). Settings, Playback sets the folder, format (PNG by default, JPG or WebP) and the file name template. These three are THC's: `screenshot-directory`, `screenshot-format` and `screenshot-template` in `mpv.conf` are ignored.

## Queue, search, history and more (0.2.0)

- **Queue:** the Queue button (hotkey `Q`) opens a panel with one queue for music and audiobooks and one for video. Add songs, albums, episodes, seasons, shows or books with the Add buttons or the right-click menu (also the Menu key); "Play next" puts something right after the current item. Reorder by dragging the grip or with Alt+Up/Down, remove with Delete, shuffle, repeat, Export and Import as M3U8 (the system file dialog). Played items stay above the current one as a backlog (kept between sessions only if you turn that on). Videos in the queue play one after another in a single window.
- **Music search:** a field at the top of Music (hotkey `/`) with All, Artists, Albums and Songs. It ignores case, accents and punctuation, ranks exact and prefix matches first, and forgives one typo in words of four or more letters.
- **Listening history:** Recently played and Most played rows, plays counted per song (threshold in Settings), album pages say how many tracks you played, long songs resume. Each portal has its own history switch, there is a Private session, and Settings, Privacy can clear each kind and export everything as JSON or CSV.
- **Media keys and now playing:** media keys control THC; on Linux THC appears as an MPRIS player (`playerctl` works). Video shares nothing with the system by default; Settings, System decides what is shown. Windows and macOS system-wide keys and lock-screen widgets are not built yet: there the keys work while THC's window is focused.
- **Video on top:** Settings, System keeps the video window on top (Off, While playing, Always), ignoring fullscreen by default; `Ctrl+T` cycles it. On Wayland the compositor decides; the window has the app id `thc` for rules.
- **Block network in MPV** (Settings, Privacy, on by default): no youtube-dl, no web addresses and a local-only protocol whitelist for THC's MPV, whatever `mpv.conf` says.
- **thc-inspect:** `thc-inspect` lists the files THC wrote, the history database tables, a running THC's network sockets (Linux), whether THC's program imports network functions, and whether MPV could go online; `--media <folder> --snapshot f` then `--compare f` proves your media was not touched. `--json` for scripts; exit code 0 pass, 1 findings, 2 error. Settings, Privacy lists what THC stores.

## Watch history

THC stores and displays your viewing history right under each video file: a playback bar showing how far you got, and a checkmark once you've watched it. Keeping up with a show is a glance at the screen.

Media you play opens in MPV, and THC picks up your progress from there. The video home page builds on this history:

- **Continue watching:** everything you stopped partway through, newest first, with a big banner for the most recent one.
- **Up next:** the next unwatched episode of each show you've been watching.
- **Show pages** show progress per show and per season, and can mark a whole season watched or unwatched. The **W** key toggles watched on the focused video.

## Audio and audiobooks

- **Music** plays inside THC, with a player bar at the bottom: previous, play/pause, next, skip back and forward, a seek bar and volume. An album keeps playing through its tracks, whatever their formats.
- **Audiobook support** covers **M4B** and **MP3**, with features suited to listening to books:
  - your place is remembered in every book and every part of it, and **Resume** picks up where you stopped;
  - playback **speed** from 0.75x to 2.5x, keeping the voice's pitch (through MPV);
  - a **sleep timer** (15, 30 or 60 minutes, or the end of the chapter);
  - **skip** back or forward by a set number of seconds (30 by default);
  - **chapter skipping** inside M4B files;
  - **Start over**, and finished parts get the same checkmark as watched videos.
- **Audio effect plugins** are planned for a later release.

## Supported formats

| Type | Handled by |
| --- | --- |
| Video files | [MPV](https://mpv.io) (through libmpv) |
| FLAC and WAV (8 to 32-bit, float, A-law, mu-law, ADPCM) | [lib-Peep](https://github.com/Doggydoggywhatnow/lib-Peep) |
| MP3 | [dr_mp3](https://github.com/mackron/dr_libs) (public domain or MIT-0), plugged into lib-Peep's codec interface |
| OGG (Vorbis) | [stb_vorbis](https://github.com/nothings/stb) (public domain or MIT), plugged into lib-Peep's codec interface |
| M4A, AAC, Opus and other audio | MPV, without a window |
| Audiobooks | M4B and MP3 (through MPV when available, so speed changes keep the pitch and M4B chapters work) |

lib-Peep is used from its `dev` branch (version 0.5.0), which has both x86-64 (NASM) and ARM64 assembly. It builds for Windows and Linux on x86-64, Linux on ARM64, and Apple Silicon Macs. On Apple Silicon THC turns on the sources' `APPLE` mode, and leaves out the compressed-WAV codec (A-law, mu-law, ADPCM), which does not assemble for macOS yet. Where lib-Peep cannot be built (for example Intel Macs, which would need underscore-prefixed x86 symbols), FLAC, WAV, MP3 and OGG play through MPV instead.

## Platforms

THC is available on **Windows**, **Linux**, and **macOS**. On Linux it runs on **Wayland** or **X11**; it isn't tied to any particular display server.

THC ships as a **separate, dedicated build for each operating system**, rather than a single universal package.

Platform notes for 0.2.0:

- CI builds and tests THC on Windows (x86-64, MSVC), Linux (x86-64) and macOS (Apple Silicon) on every push, including lib-Peep's assembly.
- Video playback opens MPV's own window through libmpv. This was tested on Linux; on macOS, libmpv's ability to open its own window depends on how libmpv was built, which has not been tested yet.

## How it works

THC is a graphical layer on top of proven components:

- **Video** is played through MPV, which handles decoding, rendering, and a broad range of container and codec support. THC loads **libmpv** when it starts (it is not linked into THC) and lets MPV open its own window, so MPV's on-screen controls, key bindings, scripts and shaders all work as usual.
- **Lossless audio** (FLAC and WAV) is handled by [lib-Peep](https://github.com/Doggydoggywhatnow/lib-Peep).
- **Other audio formats:** OGG and MP3 use public domain codecs (stb_vorbis and dr_mp3), plugged into lib-Peep's codec interface. M4B, M4A, AAC and Opus play through MPV without a window.
- **Interface** is built with SDL3 and runs the same way on Windows, Linux (Wayland and X11), and macOS. SDL3 also plays THC's own audio.
- **Watch history** is stored in a local SQLite database in your OS's standard data directory. Entries are keyed by relative path within the library plus file size and duration, so renaming or moving a file doesn't lose its history.
- **Full MPV configuration:** every MPV option is available to THC, while still tracking your history.
- **Playback state** (last position and watched status) is stored locally on your device and never transmitted. Every version of THC reads the history of every other: newer versions upgrade the database in place (keeping a `.bak` copy first), and older versions leave newer data untouched.

THC has its own separate area for configuring the MPV it uses. That means you can keep MPV installed standalone with your own settings, and THC's setup won't interfere with it (and vice versa).

## Interface design

There were no mockups; these are the decisions behind the look, so it stays consistent:

- **A streaming-service layout.** A top bar with the THC logo, the three portals (Video, Music, Audiobooks). The **logo is the way in to About and Settings** (below). Pages scroll vertically; rows of cards scroll sideways. Cards lift slightly under the mouse or keyboard focus.
- **Each portal has its own look,** in both dark and light mode:
  - **Video, "Cinema":** deep navy with an ember (orange to rose) accent. Tall 2:3 posters, wide 16:9 cards for Continue watching and Up next, a large banner at the top.
  - **Music, "Vinyl":** plum night with mint and violet. Round artist avatars, square album covers, an album page with the cover beside the track list.
  - **Audiobooks, "Library":** warm paper and ink with an amber accent. Covers are drawn as books with a spine.
  - **NSFW, "After hours" (off by default):** wine and neon pink with an amber accent, and a **beer bottle with an XXX label** as its icon (three small crosses at small sizes). The tab always has a text label too.
- **The playback bar and checkmark** sit under every video: an accent-colored bar for progress, which turns green with a round checkmark once watched. Shows use the same bar for "episodes watched out of all".
- **Art:** `poster`, `cover`, `folder`, `front`, `album` or `show` images (`.jpg` or `.png`) next to your media are used as artwork. Without one, THC draws a gradient made from the title, so every show still gets a distinct card.
- **Type:** Atkinson Hyperlegible, a typeface designed by the Braille Institute for low-vision readers, bundled with THC. Characters it lacks (Japanese titles, for example) fall back to fonts already on your system.
- **Accessibility first:** everything is reachable with the keyboard and has a visible focus ring; text size and interface scale are separate; high-contrast and reduced-motion modes; the system's light or dark setting is followed by default.
- **Drawing:** THC draws its interface itself on top of SDL3 (anti-aliased shapes, soft shadows, vector icons), with no toolkit dependency, so it looks the same everywhere.

### About and Settings

Click the **THC logo** (or press **Ctrl+,**) to open one scrolling page. At the top is **About**: the logo with a small version number, what THC is, a link to the project, the licenses of what it is built with. **Scroll past it, or press "Jump to settings", and the settings begin**, in sections (Library, Playback, Appearance, Accessibility, Hotkeys, MPV, Privacy). Click the logo again, press Escape, or choose a portal to leave. The logo has a visible focus ring and a tooltip, and works from the keyboard.

## NSFW portal (0.3.0)

An optional, **separate portal** for the files you would rather keep apart. It is **off by default**: until you turn on **Settings, Privacy, Enable the NSFW portal** there is no NSFW tab, no "Mark as NSFW" action and nothing changes anywhere.

- **Marking.** Right-click a video, episode, movie, song, album, book, show, season or any folder and choose **Mark as NSFW**, or press **N** on the selected item (it unmarks inside the NSFW portal). A folder mark covers everything inside it, **including files added later**. In **Settings, Library** a whole library folder can count as NSFW. Optional **rules** (names or patterns such as `NSFW` or `*xxx*`, not case sensitive) mark matching folders, but never override an explicit **Not NSFW**. Marked items leave the normal portals at once, with an **Undo** in the toast.
- **Only presentation.** Marks live in THC's own history database (schema version 3); **nothing on disk is touched, moved or renamed.** A marked file follows the file if it moves, using the same rule as history: only when its old path is really gone.
- **The NSFW tab** (after Audiobooks) has sub-tabs Video, Music, Audiobooks and Screenshots. They are the normal screens, showing only NSFW items. Its look is "After hours" (see Interface design).
- **Hide in one action.** **Ctrl+Shift+H**, a right-click on the tab ("Hide NSFW tab") or a switch in Settings hides the tab at once and leaves the portal; the same key shows it again. *Hidden is not off:* marked items stay out of the normal portals and nothing is deleted. Hiding stops NSFW playback by default. The state is remembered.
- **Lock (optional).** A PIN or passphrase for the portal and its settings, stored only as a **salted PBKDF2-HMAC-SHA-256 hash (50,000 iterations)** from THC's own public-domain SHA-256. THC locks again after a while (10 minutes by default), on the panic key and when the tab is hidden. **This is a courtesy lock, not security:** your media files and THC's data are not encrypted. "Reset NSFW lock" clears the PIN after you type RESET and deletes nothing.
- **Panic key (Ctrl+Shift+X).** Stops NSFW playback, closes the MPV window, silences audio, leaves the portal, locks it and removes NSFW items from every queue and backlog. It also works **inside the MPV window**. Pressing it repeatedly is safe.
- **No leaks.** NSFW items are never in Continue watching, Up next, recently added or played, counts, music search, queue suggestions or the normal screens (those read a copy of the library without them). **No history, plays or backlog** are recorded for them unless you turn that on. The MPV window title, the on-screen display and screenshot names say only "THC", media keys and the desktop's now-playing show only "Playing", and NSFW titles are hidden in the queue and the playback bar. **Thumbnails** go in `thumbs/nsfw/` and **screenshots** taken while an NSFW item plays in `screenshots/nsfw/`; both are removed when THC exits (a setting). THC prints no NSFW names in logs. `thc-inspect` reports the number of marks and never prints the name of an NSFW item.
- **Limits.** MPV scripts you install yourself are outside THC's control and may show or save titles. The debugging switch `THC_MPV_LOG` makes MPV print what it opens. A backup includes the marks only with "Include watch and listening history". The marks list can be exported and imported as plain text, which needs the lock open.

Not in this release: a blur or warn-first mode in the main portals, a boss key, search inside the NSFW portal, and showing why an item is NSFW.

## Watch tracking and MPV configuration

THC embeds MPV through **libmpv**, so it can track your viewing without taking anything away from MPV.

### Tracking watch times

1. **Resume:** when you play a file, THC looks up its saved position and starts MPV there with MPV's `start` option (always set, so "from the beginning" is explicit too).
2. **Observe:** while the file plays, THC watches MPV's own properties (`time-pos`, `duration`, `pause`, and `eof-reached`) and listens for MPV's playback events (such as end-of-file).
3. **Save:** THC writes your position to its local SQLite database periodically, and also on pause, on stop, and when you quit, so a crash loses at most a few seconds.
4. **Mark as watched:** once you pass the **watched threshold** (by default, 90% of the file), THC records the file as watched and shows the checkmark. That way skipping the credits still counts. The threshold is configurable.

THC only reads these properties. It doesn't change playback, and it never writes into your media files.

**THC's history is the only record of where you stopped.** MPV has a resume system of its own (`watch_later` files), which could compete with THC's and bring back an older position. So THC switches it off for its own MPV: `resume-playback` and `save-position-on-quit` are forced to `no`, and `watch-later-directory` is pinned inside THC's own folder, whatever your `mpv.conf` says. Everything else in `mpv.conf` still applies. *Clear watch history* also clears anything MPV left in THC's folder.

### Keeping every MPV setting

Because THC runs real MPV underneath, **all of MPV's config options remain available**: `mpv.conf`, `input.conf`, scripts, shaders, profiles, and any option you could pass on MPV's command line. Anything you'd normally set in MPV works in THC, and playback is tracked on top of it.

### THC's MPV is separate from your MPV

THC has its **own MPV configuration directory**, used only by THC. THC points libmpv at that directory instead of your system's MPV config folder, which means:

- THC never reads or changes your standalone MPV's config, scripts, or watch-later data.
- Your standalone MPV never picks up THC's settings.
- You can keep MPV installed on its own with one setup, and run THC with a completely different one.

Want THC to start from your existing MPV setup? Copy your config files into THC's MPV configuration directory. The two stay independent afterward.

### Where THC's MPV files go

THC's MPV folder is `~/.local/share/THC/THC/mpv/` on Linux (Settings → MPV → Open folder shows the exact path on any OS). It uses the same layout as MPV's own config folder:

```
mpv/
├── mpv.conf          options
├── input.conf        key bindings
├── script-opts/      settings for scripts, one <script>.conf file each (for example osc.conf)
├── scripts/          your Lua, JavaScript and C scripts
├── shaders/          shaders; "~~/shaders/x.glsl" in mpv.conf points here
└── fonts/
```

**Controller (OSC) settings go in `script-opts/osc.conf`, not `mpv.conf`.** Settings such as `layout` belong to MPV's on-screen controller script, which reads them from its own file. They are not MPV options, so on a line in `mpv.conf` MPV only logs "option not found" and skips it. For example, to put the controller at the top, create `script-opts/osc.conf` containing:

```
layout=topbar
```

Other values are `bottombar` (the default), `box` and `slimbox`. The same file takes the controller's other options, such as `windowcontrols`, `seekbarstyle`, `boxalpha` and `hidetimeout`; see the *On Screen Controller* section of the [MPV manual](https://mpv.io/manual/master/) for the full list. Restart THC fully after changing it. The controller only appears while the mouse moves over the video.

Settings → MPV has a **Copy path** button for each of these files, a **Create** button and layout picker for `osc.conf`, a list of your shaders with buttons that copy the path or the `mpv.conf` line, and a **Config check** that lists every line MPV skips, with its line number. (`THC_MPV_LOG=1 thc` in a terminal shows MPV's own messages too.)

A few options are fixed by THC so its watch history stays correct, and `mpv.conf` cannot change them: `resume-playback`, `save-position-on-quit`, `watch-later-directory` and the start position. THC keeps your place itself.


## Downloads

Linux packages for **v0.2.0** (x86-64) are rebuilt by CI on every push to `dev` and stored in the [`downloads/`](https://github.com/Doggydoggywhatnow/THC/tree/dev/downloads) folder.

| Distribution | Download | Install |
| --- | --- | --- |
| Debian, Ubuntu, Mint | [thc_0.2.0_amd64.deb](https://github.com/Doggydoggywhatnow/THC/raw/dev/downloads/thc_0.2.0_amd64.deb) | `sudo apt install ./thc_0.2.0_amd64.deb` |
| Any distribution (AppImage) | [THC-0.2.0-x86_64.AppImage](https://github.com/Doggydoggywhatnow/THC/raw/dev/downloads/THC-0.2.0-x86_64.AppImage) | `chmod +x THC-*.AppImage && ./THC-*.AppImage` |
| Alpine Linux | [thc-0.2.0-r0-alpine-x86_64.apk](https://github.com/Doggydoggywhatnow/THC/raw/dev/downloads/thc-0.2.0-r0-alpine-x86_64.apk) | `sudo apk add --allow-untrusted ./thc-0.2.0-r0-alpine-x86_64.apk` |
| Void Linux | [thc-0.2.0_1.x86_64.xbps](https://github.com/Doggydoggywhatnow/THC/raw/dev/downloads/thc-0.2.0_1.x86_64.xbps) | `xbps-rindex -a thc-*.xbps && sudo xbps-install -R $PWD thc` |
| Other (tarball) | [thc-0.2.0-linux-x86_64.tar.gz](https://github.com/Doggydoggywhatnow/THC/raw/dev/downloads/thc-0.2.0-linux-x86_64.tar.gz) | unpack into `/usr` or `~/.local` |

**thc-inspect** (read-only; checks what THC stores and whether it uses the network):

| System | Download |
| --- | --- |
| Linux | [thc-inspect-0.2.0-x86_64.AppImage](https://github.com/Doggydoggywhatnow/THC/raw/dev/downloads/thc-inspect-0.2.0-x86_64.AppImage) (also installed by the .deb and tarball) |
| Windows | [thc-inspect-0.2.0-windows-x86_64.exe](https://github.com/Doggydoggywhatnow/THC/raw/dev/downloads/thc-inspect-0.2.0-windows-x86_64.exe) |
| macOS | [thc-inspect-0.2.0-macos-arm64](https://github.com/Doggydoggywhatnow/THC/raw/dev/downloads/thc-inspect-0.2.0-macos-arm64) |

Video playback needs MPV's library (libmpv) on your system: `libmpv2` on Debian/Ubuntu, `mpv-libs` on Alpine, `mpv` on Void. The .deb, .apk and .xbps pull it in; the AppImage and tarball use whatever is installed. Music works without it.

Packages are built by `.github/workflows/packages.yml`; the recipes live in `packaging/` (AppImage script, Alpine `APKBUILD`, Void `template`) . A Snap package is planned (recipe in `snap/snapcraft.yaml`), as are Windows and macOS packages.

## Building from source

Requirements:

- A C11 compiler (GCC, Clang, or MSVC) and CMake 3.20 or newer
- [NASM](https://nasm.us) on x86-64, for lib-Peep's assembly (ARM64 uses the compiler's own assembler)
- SDL3 (if it is not installed, CMake downloads and builds it)
- SQLite (if it is not installed, CMake downloads the amalgamation)
- lib-Peep, dr_mp3, stb and the font are included in the repository (`third_party/`, `assets/`)
- At run time, **libmpv** for video (and M4B/M4A/Opus audio)

```bash
git clone <repository-url> THC
cd THC
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

Getting the dependencies:

- **Linux (Debian/Ubuntu):** `sudo apt install build-essential cmake nasm libsqlite3-dev libmpv2` plus SDL3's build dependencies (X11 and Wayland development packages) if your distribution has no SDL3 package yet.
- **macOS:** `brew install cmake sdl3 sqlite mpv`
- **Windows:** Visual Studio with C++ and CMake, plus NASM. For video, put `libmpv-2.dll` next to `thc.exe` (or set its path in Settings → MPV).

If libmpv lives somewhere unusual, set `THC_LIBMPV` to its full path or choose it in **Settings → MPV**. Build options: `-DTHC_USE_PEEP=OFF` plays FLAC, WAV, MP3 and OGG through MPV instead; `-DTHC_FETCH_DEPS=OFF` turns off the downloads.

The tests cover the watch history database, the folder parser, the settings file, and audio decoding through lib-Peep.

## Usage

```bash
thc
```

Launch THC, pick your music and video folders (or skip them), and start browsing.

Default keys (all can be changed in **Settings → Hotkeys**):

| Key | Action |
| --- | --- |
| Arrow keys | Move between items |
| Tab / Shift+Tab | Next / previous item |
| Enter | Open or play |
| Escape / Backspace | Back |
| 1 / 2 / 3 | Video / Music / Audiobooks |
| Ctrl+, | Settings |
| Space | Play or pause music or an audiobook |
| Ctrl+Right / Ctrl+Left | Next / previous track |
| J / L | Skip back / forward |
| - / = | Volume down / up |
| W | Mark the focused video watched or not |
| Ctrl+= / Ctrl+- | Larger / smaller text |
| F2 | Switch light and dark |
| F5 | Rescan libraries |
| F11 | Fullscreen |

Inside the video window, MPV's own keys apply (and THC's `input.conf`).

## Configuration

THC lets you change many things to suit you, all in **Settings**:

### Library
- Library folders (music, video, audiobooks) and a folder for each section
- How your files are sorted and presented, and which section a show appears in
- **Video thumbnails:** on or off, and clear the cache
- Rescan

### Playback
- **Watched threshold:** the point at which a file counts as finished (default 90%)
- Volume, audiobook speed and skip length
- THC's own MPV configuration, kept separate from any standalone MPV, with all of MPV's options available
- **Editable hotkeys:** keyboard shortcuts can be remapped

### NSFW portal
- Off by default; enable, hide, history, thumbnails, title masking, rules, library folders, lock timer and the PIN, all in **Settings, Privacy**. Hotkeys (mark **N**, panic **Ctrl+Shift+X**, hide or show the tab **Ctrl+Shift+H**) are remappable.

### Appearance
- **Light and dark mode**, or follow the system
- **Fonts:** the bundled Atkinson Hyperlegible or any `.ttf`/`.otf` file, and text size
- Interface scaling
- Accent colors (portal default, Ember, Rose, Ocean, Mint, Violet, Gold)

### Accessibility
- **High-contrast mode**
- **Large text and interface scaling** for easier reading (75% to 200%, set separately)
- **Full keyboard navigation** of the whole interface
- **Reduced motion:** turn off animations and transitions
- **Subtitle styling:** font, size, color, and background (through MPV)
- All of MPV's built-in accessibility features
- Screen reader support *(planned)*

### Where THC keeps its files

Everything lives in your OS's standard data directory:

| OS | Folder |
| --- | --- |
| Linux | `~/.local/share/THC/THC/` (or `$XDG_DATA_HOME/THC/THC/`) |
| macOS | `~/Library/Application Support/THC/THC/` |
| Windows | `%APPDATA%\THC\THC\` |

Inside it: `settings.ini` (plain text, editable while THC is closed), `history.db` (watch history), `screenshots/` (pictures taken with MPV's `S` key), and `mpv/` (THC's own MPV configuration: `mpv.conf`, `input.conf`, `scripts/`, `shaders/`).

**Same folder for every package.** The .deb, AppImage, tarball, Alpine and Void packages, a build from source, and the Windows and macOS builds all use the folder above, so switching between them keeps your settings and history. Installing, upgrading and uninstalling never touch it: none of THC's package scripts removes it (to start fresh, delete the folder yourself). The planned Snap runs confined and keeps its data in its own folder under `~/snap/`, which other packages do not share; use **Back up and restore** to move between a Snap and anything else.

**Before reinstalling, back up `mpv.conf` and the `mpv/` folder**, or use **Settings, Privacy, Back up to a folder**. A backup is a plain folder with `settings.ini` and `mpv/` (and, if you tick the boxes, `history.db` and screenshots), never your media or caches. **Restore from a folder** shows what the backup holds and which THC wrote it, asks first, saves your current files into a `pre-restore/` folder, and applies the backup the next time THC starts. A backup from a newer THC restores what this THC understands. Settings that an older THC does not know are kept in `settings.ini` and written back unchanged, so going back to an older version does not lose them.

## Privacy

Privacy is a core value, not a feature toggle.

- All data stays on your device.
- Watch history and resume positions are stored locally.
- THC makes no network requests to report what you play.
- You can inspect and delete your stored data at any time.

What you watch is your business. Period.

## Accessibility

THC builds on MPV, which has a wide range of accessibility features, and adds user-adjustable settings on top. If something gets in your way, please [open an issue](../../issues). Accessibility problems are treated as bugs.

## Roadmap

- [x] Core playback for video and audio
- [x] First-launch folder setup for music and video
- [x] Music and video portals
- [x] Library sections (Anime, Television, Cartoons, Movies)
- [x] Resume from last position
- [x] Watched tracking with playback bar and checkmark
- [x] Separate MPV configuration for THC
- [ ] Audio effect plugins (later)
- [x] Audiobook features (resume, speed, sleep timer, skip, chapters)
- [x] Editable hotkeys
- [x] Windows, Linux (Wayland and X11), and macOS builds (built and tested in CI)
- [ ] Screen reader support
- [x] Tags and embedded cover art from audio files
- [x] Video thumbnails
- [x] The logo opens About and Settings
- [ ] Gapless music playback
- [ ] *(future, optional)* Fetching episode and season descriptions. This would be strictly opt-in and off by default, to keep THC private.

## FAQ

### Why not just use MPV?

MPV is a fantastic player, and THC is built on it. But MPV is a player, not a library browser. You open a file, it plays, and that's it. It doesn't know what show you're in, which episodes you've finished, or where you left off last night.

THC adds the layer around MPV:

- **A library to browse,** built from your existing folders, instead of hunting through a file manager
- **Shows organized as show → seasons → episodes**
- **Watch history:** a playback bar and checkmark on every video
- **Separate music, video, and audiobook experiences**
- **A streaming-service feel** for files you already own

MPV still does the playing, so you keep its decoding, quality, and accessibility features. And because THC has its own MPV configuration, you can keep a standalone MPV installed with your own settings, and the two won't interfere.

If you just want to play one file, MPV is perfect. If you want your local library to feel like a streaming service, that's THC.

### Does THC modify my files?

No. THC doesn't write anything into your media files, and it doesn't move, rename, or reorganize them. Changing the sorting in THC only changes how your folders are presented inside THC; your files stay exactly where they are. Your watch history and resume positions are stored separately by THC on your device.

### Does THC need an internet connection?

No. THC works entirely offline and makes no network requests to report what you play. A future, optional feature may fetch episode and season descriptions, but it would be opt-in and off by default.

## Contributing

Contributions are welcome once the project is public. Ideas, bug reports, and pull requests that fit THC's four values are especially appreciated. Contribution guidelines will be added here.

## Acknowledgments

- [MPV](https://mpv.io) for video playback and its excellent accessibility features
- [lib-Peep](https://github.com/Doggydoggywhatnow/lib-Peep) for FLAC and WAV support
- The authors of the public domain codecs THC relies on

## Versioning

THC uses `x.y.z` version numbers:

- **`x.0.0` (major):** major releases
- **`0.y.0` (minor):** big new features
- **`0.0.z` (patch):** small new features, bug fixes, and security updates

## License

THC's own source code is released under [the Unlicense](https://unlicense.org), which dedicates it to the public domain.

Bundled third-party code and assets keep their own licenses:

| Component | Use | License |
| --- | --- | --- |
| [lib-Peep](https://github.com/Doggydoggywhatnow/lib-Peep) | WAV and FLAC decoding | Unlicense (public domain) |
| [dr_mp3](https://github.com/mackron/dr_libs) | MP3 decoding | Public domain (Unlicense) or MIT-0 |
| [stb_vorbis, stb_truetype, stb_image](https://github.com/nothings/stb) | OGG decoding, text, cover images | Public domain or MIT |
| [Atkinson Hyperlegible](https://brailleinstitute.org/freefont) | The default interface font | SIL Open Font License 1.1 (`assets/fonts/OFL.txt`) |
| [SDL3](https://libsdl.org) | Window, input, audio output | zlib |
| [SQLite](https://sqlite.org) | Watch history | Public domain |

THC does not link MPV. It loads libmpv at run time (`libmpv.so.2`, `libmpv-2.dll` or `libmpv.2.dylib`), so no MPV code is part of a THC build. libmpv is licensed under the GPL or, when built with `-Dgpl=false`, the LGPL; an LGPL build is recommended. MPV keeps its own license.
