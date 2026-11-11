# Changelog

THC uses `x.y.z` versions: major, minor (big features), patch (small features, fixes, security).

## 0.1.1

### Added
- The THC logo opens About and Settings on one scrolling page, with the version number. The gear button is gone. (#10)
- Tags from audio files: titles, artists, albums, track and disc numbers, year, genre and embedded cover art for MP3, FLAC, OGG, M4B and WAV, plus narrator and chapter count for audiobooks. Albums are ordered by track number. (#8)
- Video thumbnails: a picture next to the video, or a frame grabbed from it, shown on episodes and posters. Can be turned off or cleared in Settings. (#9)

### Fixed
- THC sometimes resumed from an older position because MPV's own resume competed with THC's history. THC's history is now the only source, whatever `mpv.conf` says. (#5)

## 0.1.0

- First demo: video, music and audiobook portals, resume and watched tracking, library sections, THC's own MPV configuration, settings, hotkeys and accessibility options.
