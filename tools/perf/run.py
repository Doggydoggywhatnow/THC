#!/usr/bin/env python3
"""Measure THC against a synthetic library (docs/performance.md, #12).

  run.py <path to thc> [sizes ...]        e.g.  run.py build/thc 1000 10000 50000

For each size it builds a library in a temporary folder (make_library.py), runs THC headless (xvfb-run) with
THC_PERF=1, which scans, reads tags, times a search and a 10,000-item queue, scrolls the Music screen and prints one JSON
line, and prints a Markdown table row per size. Nothing is written outside the temporary folder."""
import json, os, platform, shutil, subprocess, sys, tempfile, time

here = os.path.dirname(os.path.abspath(__file__))


def one(thc, size):
    root = tempfile.mkdtemp(prefix="thc-perf-")
    try:
        subprocess.run([sys.executable, os.path.join(here, "make_library.py"), root, str(size)], check=True,
                       stdout=subprocess.DEVNULL)
        data = os.path.join(root, "xdg", "THC", "THC")
        os.makedirs(data)
        with open(os.path.join(data, "settings.ini"), "w") as f:
            f.write("setup_done = 1\nmusic_dir = %s\nvideo_dir = %s\nbooks_dir = %s\nvideo_thumbs = 0\n" % (
                os.path.join(root, "Music"), os.path.join(root, "Video"), os.path.join(root, "Books")))
        env = dict(os.environ, XDG_DATA_HOME=os.path.join(root, "xdg"), SDL_AUDIO_DRIVER="dummy", THC_PERF="1")
        cmd = [thc]
        if shutil.which("xvfb-run") and not os.environ.get("DISPLAY"):
            cmd = ["xvfb-run", "-a", "-s", "-screen 0 1280x800x24", thc]
        t = time.time()
        r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=1800)
        wall = time.time() - t
        line = [l for l in r.stdout.splitlines() if l.startswith("{")]
        if not line:
            sys.exit("no result from THC (exit %d):\n%s" % (r.returncode, r.stderr[-500:]))
        res = json.loads(line[-1])
        res["wall_s"] = round(wall, 1)
        return res
    finally:
        shutil.rmtree(root, ignore_errors=True)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    thc = sys.argv[1]
    sizes = [int(x) for x in sys.argv[2:]] or [1000]
    print("<!-- %s, %s, %d CPUs -->" % (platform.platform(), platform.machine(), os.cpu_count() or 0))
    print("| files | scan | tags | first frame | scroll frame avg / p95 / max | search build | search per key (avg / max) | "
          "queue 10k add / shuffle / export | peak memory |")
    print("| --- | --- | --- | --- | --- | --- | --- | --- | --- |")
    for n in sizes:
        r = one(thc, n)
        print("| %d | %.0f ms | %.0f ms | %.0f ms | %.1f / %.1f / %.1f ms | %.1f ms | %.2f / %.2f ms | %.1f / %.1f / %.1f ms | %.0f MB |" % (
            r["files"], r["scan_ms"], r["tags_ms"], r["first_frame_ms"], r["scroll_avg_ms"], r["scroll_p95_ms"],
            r["scroll_max_ms"], r["search_build_ms"], r["search_query_avg_ms"], r["search_query_max_ms"],
            r["queue_add_10000_ms"], r["queue_shuffle_ms"], r["queue_export_ms"], r["peak_rss_kb"] / 1024.0))
        sys.stdout.flush()


if __name__ == "__main__":
    main()
