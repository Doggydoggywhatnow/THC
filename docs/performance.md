# THC performance baseline

A baseline for 0.3.0 (#12): how fast and how big THC is **before** any optimizing, so 0.4 can be compared against it.
Nothing was tuned for these numbers.

## How to measure

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
python3 tools/perf/run.py build/thc 1000 10000 50000
```

`tools/perf/make_library.py <folder> <files>` builds a synthetic library (about 70% tagged MP3s with a cover for some albums, 25%
video files in Show / Season / Episode folders, 5% audiobooks), in a temporary folder that `run.py` removes afterwards.
`run.py` starts THC headless (`xvfb-run`, so it needs `xvfb`; on a desktop it uses the display) with its own empty data folder.

### The timing hook

Setting the environment variable `THC_PERF` makes THC scan its library, read the tags, time a search and a 10,000-item queue,
scroll the Music screen for 300 frames, print **one JSON line** and exit (`src/perf.c`). It is for measuring only and is not meant
for normal use; with the variable unset none of that code runs. Fields: `scan_ms` (process start to library ready),
`tags_ms` (reading every audio file's tags), `first_frame_ms`, `scroll_avg_ms` / `scroll_p95_ms` / `scroll_max_ms` (frame
time while the mouse wheel scrolls), `search_build_ms` and `search_query_avg_ms` / `max` (one keystroke across artists, albums and
songs), `queue_add_10000_ms`, `queue_shuffle_ms`, `queue_export_ms`, `queue_remove_1000_ms`, `peak_rss_kb`.

## Results

Measured on Linux x86-64 (4 CPUs, a cloud container, software rendering under Xvfb, local disk with a warm cache), THC built
with CMake's default flags. **Cold-cache scans from a slow disk or a network share will be slower.** Frame times of about 16.7 ms
are the 60 Hz vsync, not a cost.

| files | scan | tags | first frame | scroll frame avg / p95 / max | search build | search per key (avg / max) | queue 10k add / shuffle / export | peak memory |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1,030 | 191 ms | 26 ms | 190 ms | 16.9 / 16.7 / 84.8 ms | 0.2 ms | 0.09 / 0.23 ms | 14.1 / 3.1 / 3.0 ms | 196 MB |
| 10,292 | 245 ms | 81 ms | 223 ms | 16.6 / 16.8 / 18.8 ms | 3.5 ms | 1.22 / 3.00 ms | 18.6 / 4.7 / 4.6 ms | 210 MB |
| 51,459 | 453 ms | 307 ms | 240 ms | 17.8 / 22.0 / 38.2 ms | 24.3 ms | 5.16 / 11.09 ms | 21.3 / 3.6 / 3.7 ms | 265 MB |

Windows and macOS were not measured here (no machine); the script and hook run there too and the table should get rows for them.

## Targets

A target is a line to hold, not a promise of a number on every machine.

| What | Target at 50,000 files |
| --- | --- |
| Scan, warm cache | under 1 s |
| Tags read | under 2 s, never blocking the interface |
| Time to first frame | under 0.5 s |
| Scrolling | no frame over 2 vsync intervals (33 ms) |
| Search | under one frame (16 ms) per keystroke |
| Queue: add 10,000 / shuffle | under 100 ms each |
| Memory | under 300 MB, and not above 150 MB with a small library |

## What was found

- **Everything meets its target except the small-library memory baseline:** a 1,000-file library already peaks at about 196 MB, and
  memory grows only slowly after that (265 MB at 50,000 files). Most of it is not the library; filed as an issue with these numbers.
- The slowest single frame while scrolling at 1,000 files (85 ms) is the first one, which loads fonts and textures. Worth a look
  next to the memory issue.
- Search is well inside a frame up to 50,000 songs (11 ms at worst).

Nothing was optimized in 0.3.0.
