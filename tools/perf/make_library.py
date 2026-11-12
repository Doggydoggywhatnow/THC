#!/usr/bin/env python3
"""Make a synthetic THC library for the performance baseline (docs/performance.md, #12).

  make_library.py <folder> <files>

Creates <folder>/Music (tagged MP3s, a cover per album), <folder>/Video (Show/Season/Episode files) and
<folder>/Books (a few M4B-named files). About 70% music, 25% video, 5% books. The files are tiny and hold no
real audio or video: THC's scan and tag reader only need names and tags. Standard library only."""
import os, struct, sys

# a 1x1 JPEG, used as the cover of every album
JPEG = bytes.fromhex(
    "ffd8ffe000104a46494600010100000100010000ffdb004300080606070605080707070909080a0c140d0c0b0b0c1912130f14"
    "1d1a1f1e1d1a1c1c20242e2720222c231c1c2837292c30313434341f27393d38323c2e333432ffc0000b08000100010101110"
    "0ffc4001f0000010501010101010100000000000000000102030405060708090a0bffc400b5100002010303020403050504040"
    "000017d01020300041105122131410613516107227114328191a1082342b1c11552d1f02433627282090a161718191a252627"
    "28292a3435363738393a434445464748494a535455565758595a636465666768696a737475767778797a838485868788898a92"
    "939495969798999aa2a3a4a5a6a7a8a9aab2b3b4b5b6b7b8b9bac2c3c4c5c6c7c8c9cad2d3d4d5d6d7d8d9dae1e2e3e4e5e6e7"
    "e8e9eaf1f2f3f4f5f6f7f8f9faffda0008010100003f00fbfcffd9")


def syncsafe(n):
    return bytes([(n >> 21) & 127, (n >> 14) & 127, (n >> 7) & 127, n & 127])


def frame(fid, text):
    data = b"\x03" + text.encode("utf-8")
    return fid.encode() + struct.pack(">I", len(data)) + b"\x00\x00" + data


def mp3(title, artist, album, track):
    frames = frame("TIT2", title) + frame("TPE1", artist) + frame("TALB", album) + frame("TRCK", str(track))
    return b"ID3\x03\x00\x00" + syncsafe(len(frames)) + frames + (b"\xff\xfb\x90\x00" + b"\x00" * 413) * 2


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    root, total = sys.argv[1], int(sys.argv[2])
    music_n, video_n, book_n = int(total * 0.70), int(total * 0.25), int(total * 0.05)
    tracks_per_album = 12
    albums_per_artist = 4
    made = 0
    for i in range(music_n):
        album_no, track = divmod(i, tracks_per_album)
        artist_no, album_in = divmod(album_no, albums_per_artist)
        artist, album = "Artist %04d" % artist_no, "Album %d" % album_in
        d = os.path.join(root, "Music", artist, album)
        os.makedirs(d, exist_ok=True)
        title = "Track %d of %s %s" % (track + 1, artist, album)
        with open(os.path.join(d, "%02d - %s.mp3" % (track + 1, title)), "wb") as f:
            f.write(mp3(title, artist, album, track + 1))
        if track == 0 and album_in < 2:                       # a cover for some albums
            with open(os.path.join(d, "cover.jpg"), "wb") as f:
                f.write(JPEG)
        made += 1
    eps_per_season, seasons = 10, 3
    for i in range(video_n):
        show_no, rest = divmod(i, eps_per_season * seasons)
        season, ep = divmod(rest, eps_per_season)
        d = os.path.join(root, "Video", "Show %04d (2020)" % show_no, "Season %d" % (season + 1))
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "Show %04d S%02dE%02d.mkv" % (show_no, season + 1, ep + 1)), "wb") as f:
            f.write(b"\x1a\x45\xdf\xa3" + bytes(64))
        made += 1
    os.makedirs(os.path.join(root, "Books"), exist_ok=True)
    for i in range(book_n):
        d = os.path.join(root, "Books", "Author %03d" % (i // 5), "Book %d" % i)
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "Book %d.m4b" % i), "wb") as f:
            f.write(bytes(64))
        made += 1
    print("made %d files in %s" % (made, root))


if __name__ == "__main__":
    main()
