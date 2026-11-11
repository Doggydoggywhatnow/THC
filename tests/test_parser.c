#include "library.h"
#include "test_util.h"

static void ep(const char *name, int season, int episode, int end, const char *title)
{
    EpisodeInfo e;
    parse_episode(name, &e);
    if (e.season != season || e.episode != episode || e.episode_end != end || strcmp(e.title, title)) {
        fprintf(stderr, "parse_episode(\"%s\") = S%d E%d-%d \"%s\", want S%d E%d-%d \"%s\"\n", name, e.season,
                e.episode, e.episode_end, e.title, season, episode, end, title);
        g_failures++;
    }
    g_checks++;
}

static void title(const char *name, bool ext, const char *want, int want_year)
{
    char t[256];
    int y;
    clean_title(name, ext, t, sizeof t, &y);
    if (strcmp(t, want) || y != want_year) {
        fprintf(stderr, "clean_title(\"%s\") = \"%s\" (%d), want \"%s\" (%d)\n", name, t, y, want, want_year);
        g_failures++;
    }
    g_checks++;
}

static const Show *find_show(const VideoLibrary *lib, const char *t)
{
    for (int i = 0; i < lib->nshows; i++)
        if (!strcmp(lib->shows[i].title, t)) return &lib->shows[i];
    return NULL;
}

static const Movie *find_movie(const VideoLibrary *lib, const char *t)
{
    for (int i = 0; i < lib->nmovies; i++)
        if (!strcmp(lib->movies[i].title, t)) return &lib->movies[i];
    return NULL;
}

int main(void)
{
    /* Episode names */
    ep("Frieren - S01E03 - The Blue-Moon Weed.mkv", 1, 3, 3, "The Blue-Moon Weed");
    ep("The.Office.S02E05.720p.WEB-DL.mkv", 2, 5, 5, "");
    ep("show.s1e2.mp4", 1, 2, 2, "");
    ep("Show S01E01-E02 Double.mkv", 1, 1, 2, "Double");
    ep("Show S01E01E02.mkv", 1, 1, 2, "");
    ep("Severance - 1x05 - Goodbye, Mrs. Selvig.mkv", 1, 5, 5, "Goodbye, Mrs. Selvig");
    ep("Movie 1920x1080.mkv", -1, -1, -1, "");
    ep("Episode 4.mkv", -1, 4, 4, "");
    ep("Ep. 12 - Finale.mp4", -1, 12, 12, "Finale");
    ep("E07.mkv", -1, 7, 7, "");
    ep("[SubsPlease] Sousou no Frieren - 07 (1080p) [ABCD1234].mkv", -1, 7, 7, "");
    ep("[Group] Show Name - 112v2 [720p].mkv", -1, 112, 112, "");
    ep("02 - The Second One.avi", -1, 2, 2, "The Second One");
    ep("03. Third.mkv", -1, 3, 3, "Third");
    ep("Just A Title.mkv", -1, -1, -1, "");
    ep("Show - 2019 - Special.mkv", -1, -1, -1, "");

    /* Season folders */
    CHECK(parse_season_dir("Season 1") == 1);
    CHECK(parse_season_dir("season 02") == 2);
    CHECK(parse_season_dir("S03") == 3);
    CHECK(parse_season_dir("Series 4") == 4);
    CHECK(parse_season_dir("Book 1 Water") == 1);
    CHECK(parse_season_dir("Specials") == 0);
    CHECK(parse_season_dir("Extras") == 0);
    CHECK(parse_season_dir("Season.10") == 10);
    CHECK(parse_season_dir("5") == 5);
    CHECK(parse_season_dir("Behind the Scenes") == -1);
    CHECK(parse_season_dir("Seasoning") == -1);

    /* Titles */
    title("Blade Runner 2049 (2017)", false, "Blade Runner 2049", 2017);
    title("Blade.Runner.2049.2017.1080p.BluRay.x264.mkv", true, "Blade Runner 2049", 2017);
    title("Arrival.2016.1080p.mkv", true, "Arrival", 2016);
    title("The Office (US)", false, "The Office (US)", 0);
    title("[Group] Some Anime [BD 1080p]", false, "Some Anime", 0);
    title("2001 A Space Odyssey (1968).mkv", true, "2001 A Space Odyssey", 1968);
    title("1917.mkv", true, "1917", 0);
    title("Mr. Robot", false, "Mr. Robot", 0);
    title("my_home_video.mp4", true, "my home video", 0);

    /* Sections by folder name */
    CHECK(section_for_dirname("Anime") == SECTION_ANIME);
    CHECK(section_for_dirname("TV Shows") == SECTION_TV);
    CHECK(section_for_dirname("cartoons") == SECTION_CARTOONS);
    CHECK(section_for_dirname("Films") == SECTION_MOVIES);
    CHECK(section_for_dirname("Holiday") == -1);

    /* A whole library */
    FsNode *root = fs_new("/media/Videos", true);
    fs_add_path(root, "Anime/Frieren/Season 1/Frieren - S01E02 - B.mkv", 200, 2);
    fs_add_path(root, "Anime/Frieren/Season 1/Frieren - S01E01 - A.mkv", 100, 1);
    fs_add_path(root, "Anime/Frieren/Season 1/Frieren - S01E10 - J.mkv", 300, 3);
    fs_add_path(root, "Anime/Frieren/Season 2/Frieren - S02E01.mkv", 400, 4);
    fs_add_path(root, "Anime/Frieren/Specials/OVA.mkv", 500, 5);
    fs_add_path(root, "Anime/Frieren/poster.jpg", 10, 1);
    fs_add_path(root, "Anime/Your Name (2016).mkv", 900, 1);
    fs_add_path(root, "Cartoons/Avatar/Book 1 Water/Episode 2.mkv", 20, 1);
    fs_add_path(root, "Cartoons/Avatar/Book 1 Water/Episode 1.mkv", 10, 1);
    fs_add_path(root, "Cartoons/Avatar/Book 2 Earth/Episode 1.mkv", 30, 1);
    fs_add_path(root, "Loose Show/Loose.Show.S01E01.mkv", 11, 1);
    fs_add_path(root, "Loose Show/Loose.Show.S01E02.mkv", 12, 1);
    fs_add_path(root, "Loose Show/Loose.Show.S02E01.mkv", 13, 1);
    fs_add_path(root, "Arrival (2016)/Arrival.2016.mkv", 5000, 1);
    fs_add_path(root, "Arrival (2016)/Featurette.mkv", 50, 1);
    fs_add_path(root, "Arrival (2016)/poster.png", 5, 1);
    fs_add_path(root, "Paddington 2.mkv", 4000, 1);
    fs_add_path(root, "Movies/Dune Part Two (2024).mkv", 7000, 1);
    fs_add_path(root, "Not media/readme.txt", 1, 1);
    fs_sort(root, true);

    FsNode *tv = fs_new("/media/TV", true);
    fs_add_path(tv, "Severance/S01/Severance - 1x01 - Good News About Hell.mkv", 1, 1);
    fs_add_path(tv, "Severance/S01/Severance - 1x02 - Half Loop.mkv", 2, 1);
    fs_add_path(tv, "Mini/Only One.mkv", 3, 1);
    fs_sort(tv, true);

    LibraryRoot roots[2] = { { root, 0, -1 }, { tv, 4, SECTION_TV } };
    VideoLibrary lib;
    library_build(&lib, roots, 2, NULL);

    const Show *fr = find_show(&lib, "Frieren");
    CHECK(fr != NULL);
    if (fr) {
        CHECK(fr->section == SECTION_ANIME);
        CHECK(fr->nseasons == 3);
        CHECK(fr->nepisodes == 5);
        CHECK(fr->art && !strcmp(fr->art->name, "poster.jpg"));
        CHECK_STR(fr->seasons[0].name, "Season 1");
        CHECK(fr->seasons[0].neps == 3);
        CHECK(fr->seasons[0].eps[0].number == 1);
        CHECK(fr->seasons[0].eps[1].number == 2);
        CHECK(fr->seasons[0].eps[2].number == 10); /* numeric, not text, order */
        CHECK_STR(fr->seasons[0].eps[0].title, "A");
        CHECK_STR(fr->seasons[0].eps[2].label, "Episode 10");
        CHECK_STR(fr->seasons[1].name, "Season 2");
        CHECK(fr->seasons[2].number == 0); /* specials last */
        CHECK_STR(fr->seasons[2].name, "Specials");
        int s = -1, e = -1;
        CHECK(library_show_of(&lib, fr->seasons[0].eps[1].file, &s, &e) == fr && s == 0 && e == 1);
    }
    const Movie *yn = find_movie(&lib, "Your Name");
    CHECK(yn && yn->section == SECTION_ANIME && yn->year == 2016);

    const Show *av = find_show(&lib, "Avatar");
    CHECK(av && av->section == SECTION_CARTOONS && av->nseasons == 2);
    if (av) {
        CHECK_STR(av->seasons[0].name, "Book 1 Water");
        CHECK(av->seasons[0].neps == 2 && av->seasons[0].eps[0].number == 1);
    }

    /* Episodes straight in the show folder are grouped by their SxxEyy. */
    const Show *ls = find_show(&lib, "Loose Show");
    CHECK(ls && ls->section == SECTION_TV && ls->nseasons == 2);
    if (ls) CHECK(ls->seasons[0].number == 1 && ls->seasons[0].neps == 2);

    /* Movie folder: the largest video is the film; extras do not make it a show. */
    const Movie *ar = find_movie(&lib, "Arrival");
    CHECK(ar && ar->year == 2016 && ar->section == SECTION_MOVIES);
    if (ar) {
        CHECK_STR(ar->file->name, "Arrival.2016.mkv");
        CHECK(ar->art != NULL);
    }
    CHECK(find_movie(&lib, "Paddington 2") != NULL);
    const Movie *du = find_movie(&lib, "Dune Part Two");
    CHECK(du && du->section == SECTION_MOVIES && du->year == 2024);

    /* Section folder added in Settings: every folder is a show there. */
    const Show *sv = find_show(&lib, "Severance");
    CHECK(sv && sv->section == SECTION_TV && sv->root == 4);
    if (sv) CHECK(sv->nepisodes == 2 && !strcmp(sv->seasons[0].eps[1].title, "Half Loop"));
    const Show *mini = find_show(&lib, "Mini");
    CHECK(mini && mini->nseasons == 1 && !strcmp(mini->seasons[0].name, "Season 1"));
    library_clear(&lib);

    /* Presentation overrides: move Avatar to Anime, hide Paddington 2. */
    Settings set;
    settings_defaults(&set);
    settings_set_override(&set, "0:Cartoons/Avatar", SECTION_ANIME);
    settings_set_override(&set, "0:Paddington 2.mkv", -1);
    library_build(&lib, roots, 2, &set);
    av = find_show(&lib, "Avatar");
    CHECK(av && av->section == SECTION_ANIME);
    CHECK(find_movie(&lib, "Paddington 2") == NULL);
    library_clear(&lib);

    fs_free(root);
    fs_free(tv);
    TEST_DONE();
}
