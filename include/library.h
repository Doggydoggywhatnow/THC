/* library.h: turn scanned folders into shows, seasons, episodes and movies.
 *
 * Shows are expected as Show / Season / Episode (the README's layout), and
 * the parser is forgiving about how folders and files are named:
 *   seasons:  "Season 2", "S02", "Series 2", "Book 1 Water", "Specials"
 *   episodes: "S01E02", "1x02", "Episode 2", "Ep. 2", "E02", "- 02 -", "02 - Title"
 * Movies are one file or one folder each. Sorting only changes what THC
 * shows; nothing on disk is touched.
 */
#ifndef THC_LIBRARY_H
#define THC_LIBRARY_H

#include "fstree.h"
#include "settings.h"

#include <stdbool.h>
#include <stddef.h>

/* ---- name parsing (exposed for tests) ---------------------------------- */

typedef struct EpisodeInfo {
    int season;      /* -1 if the name does not say */
    int episode;     /* -1 if unknown */
    int episode_end; /* for "S01E01-E02", else = episode */
    char title[256]; /* cleaned episode title, may be empty */
} EpisodeInfo;

void parse_episode(const char *filename, EpisodeInfo *out);
/* Season number from a folder name: 0 for specials, -1 if not a season folder. */
int parse_season_dir(const char *dirname);
/* "Blade.Runner.2049.2017.1080p.BluRay" -> title "Blade Runner 2049", year 2017. */
void clean_title(const char *name, bool strip_ext, char *title, size_t cap, int *year);
/* Which section a top-level folder name stands for, or -1. */
int section_for_dirname(const char *dirname);

/* ---- the parsed video library ------------------------------------------ */

typedef struct Episode {
    const FsNode *file;
    int number;          /* episode number in the season, -1 if unknown */
    int number_end;
    char title[256];     /* episode title, may be empty */
    char label[64];      /* "Episode 3" or "Episodes 3-4" */
} Episode;

typedef struct Season {
    char name[128];      /* "Season 1", "Specials", or the folder name */
    int number;          /* 0 = specials, -1 = unnumbered */
    const FsNode *dir;   /* NULL if episodes sit directly in the show folder */
    Episode *eps;
    int neps;
} Season;

typedef struct Show {
    char title[256];
    int year;
    int root;            /* which library root it came from */
    int section;         /* SECTION_* */
    const FsNode *dir;
    const FsNode *art;
    Season *seasons;
    int nseasons;
    int nepisodes;
    int64_t added;       /* newest file time */
    char key[THC_PATH_MAX]; /* "<root>:<relative dir>" for overrides */
} Show;

typedef struct Movie {
    char title[256];
    int year;
    int root;
    int section;
    const FsNode *file;
    const FsNode *dir;   /* folder of a one-folder movie, else NULL */
    const FsNode *art;
    int64_t added;
    char key[THC_PATH_MAX];
} Movie;

typedef struct VideoLibrary {
    Show *shows;
    int nshows;
    Movie *movies;
    int nmovies;
} VideoLibrary;

/* A root to parse: the main video folder (section = -1, auto-detect) or a
 * section folder added in Settings (section = SECTION_*). */
typedef struct LibraryRoot {
    const FsNode *node;
    int id;
    int section;
} LibraryRoot;

/* Build the library. `s` supplies presentation overrides (may be NULL). */
void library_build(VideoLibrary *lib, const LibraryRoot *roots, int nroots, const Settings *s);
void library_clear(VideoLibrary *lib);

/* Find the show or movie that a file belongs to (NULL if none). */
const Show *library_show_of(const VideoLibrary *lib, const FsNode *file, int *season, int *episode);
const Movie *library_movie_of(const VideoLibrary *lib, const FsNode *file);

#endif
