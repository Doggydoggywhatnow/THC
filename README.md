# THC

**A lightweight multimedia player for the music and videos you already have.**

THC is a graphical interface for your local media, written in C. It shows what's already on your device, built around the way you already organize your files, and makes browsing it feel as unique and enjoyable as booting up a streaming service, without the streaming service. No accounts, no uploads, no subscriptions: just your library, on your device.

> **Status:** early development. Sections marked *(planned)* describe where THC is headed rather than what ships today.

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

## Features

- A graphical interface for the media you already have
- Works from your existing folder structure, with the option to change how things are sorted
- Separate, dedicated **music** and **video** libraries, each with its own look
- Optional library sections: Anime, Television, Cartoons, and Movies
- **Resume playback:** remembers your last position in each video file
- **Watched tracking:** a playback bar and checkmark under each video
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

Each folder gets its own "portal," a distinct design for displaying its files. You can set up one, both, or neither and add more later.

## Library sections

THC reads your files as they're already sorted, but you can change that sorting from within THC. Beyond your main folders, you can add sections later. The available sections are:

- **Anime**
- **Television** (live action)
- **Cartoons**
- **Movies**

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

### Movies and music

- **Movies:** one file or one folder per movie.
- **Music:** artist → album → track.

## Watch history

THC stores and displays your viewing history right under each video file: a playback bar showing how far you got, and a checkmark once you've watched it. Keeping up with a show is a glance at the screen.

Media you play opens in MPV, and THC picks up your progress from there.

## Audio and audiobooks

- **Audio effect plugins** are planned for a later release.
- **Audiobook support** covers **M4B** and **MP3**, along with features suited to audiobooks. *(planned: feature details to be documented.)*

## Supported formats

| Type | Handled by |
| --- | --- |
| Video files | [MPV](https://mpv.io) |
| FLAC and WAV | [lib-Peep](https://github.com/Doggydoggywhatnow/lib-Peep) |
| OGG and MP3 | Public domain codecs |
| Audiobooks | M4B and MP3 |

## Platforms

THC is available on **Windows**, **Linux**, and **macOS**. On Linux it runs on **Wayland** or **X11**; it isn't tied to any particular display server.

THC ships as a **separate, dedicated build for each operating system**, rather than a single universal package.

## How it works

THC is a graphical layer on top of proven components:

- **Video** is played through MPV, which handles decoding, rendering, and a broad range of container and codec support.
- **Lossless audio** (FLAC and WAV) is handled by [lib-Peep](https://github.com/Doggydoggywhatnow/lib-Peep).
- **Other audio formats** (OGG, MP3, M4B) use public domain codecs.
- **Interface** is built with SDL3 and runs the same way on Windows, Linux (Wayland and X11), and macOS.
- **Watch history** is stored in a local SQLite database in your OS's standard data directory. Entries are keyed by relative path within the library plus file size and duration, so renaming or moving a file doesn't lose its history.
- **Full MPV configuration:** every MPV option is available to THC, while still tracking your history.
- **Playback state** (last position and watched status) is stored locally on your device and never transmitted.

THC has its own separate area for configuring the MPV it uses. That means you can keep MPV installed standalone with your own settings, and THC's setup won't interfere with it (and vice versa).

## Building from source

*(planned: build instructions will be added as the project takes shape.)*

Expected requirements:

- A C compiler (GCC, Clang, or MSVC)
- A build tool (Make or CMake)
- libmpv development headers
- SDL3
- SQLite
- [lib-Peep](https://github.com/Doggydoggywhatnow/lib-Peep)

```bash
git clone <repository-url> THC
cd THC
# build steps coming soon
```

## Usage

*(planned)*

```bash
thc
```

Launch THC, pick your music and video folders (or skip them), and start browsing.

## Configuration

THC lets you change many things to suit you, including:

- Library folders and sections
- How your files are sorted
- THC's own MPV configuration, kept separate from any standalone MPV
- **Editable hotkeys:** keyboard shortcuts can be remapped
- Appearance
- Where playback state is stored

Full option documentation will be added as settings are finalized.

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

- [ ] Core playback for video and audio
- [ ] First-launch folder setup for music and video
- [ ] Music and video portals
- [ ] Library sections (Anime, Television, Cartoons, Movies)
- [ ] Resume from last position
- [ ] Watched tracking with playback bar and checkmark
- [ ] Separate MPV configuration for THC
- [ ] Audio effect plugins (later)
- [ ] Audiobook features
- [ ] Editable hotkeys
- [ ] Windows, Linux (Wayland and X11), and macOS builds
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
- **`0.x.0` (minor):** big new features
- **`0.0.x` (patch):** small new features, bug fixes, and security updates

## License

THC's own source code is released under [the Unlicense](https://unlicense.org), which dedicates it to the public domain.

THC uses MPV, which is licensed under the GPL (or LGPL, depending on how it's built). MPV keeps its own license, and builds of THC that include or link MPV are distributed in line with MPV's license terms.
