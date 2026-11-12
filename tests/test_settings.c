#include "settings.h"
#include "test_util.h"

int main(void)
{
    const char *path = "thc_test_settings.ini";
    remove(path);
    Settings s, t;
    settings_defaults(&s);
    CHECK(!settings_load(&s, path)); /* missing file keeps defaults */
    CHECK_NEAR(s.watched_threshold, 0.90);
    CHECK_STR(s.hotkey[ACT_PLAY_PAUSE], "Space");
    CHECK(s.history_video && s.history_music && s.history_books);
    CHECK(s.video_thumbs);   /* thumbnails are on by default */

    s.setup_done = true;
    snprintf(s.music_dir, sizeof s.music_dir, "/home/me/Music");
    snprintf(s.video_dir, sizeof s.video_dir, "C:\\Users\\me\\Videos = mine");
    snprintf(s.section_dir[SECTION_ANIME], sizeof s.section_dir[0], "/media/Anime");
    s.watched_threshold = 0.8;
    s.theme = THEME_LIGHT;
    s.text_scale = 1.5f;
    s.high_contrast = true;
    s.video_thumbs = false;
    snprintf(s.hotkey[ACT_SEEK_BACK], sizeof s.hotkey[0], "Left");
    settings_set_override(&s, "video:Avatar The Last Airbender", SECTION_CARTOONS);
    settings_set_override(&s, "video:Some Show", SECTION_ANIME);
    CHECK(settings_save(&s, path));

    settings_defaults(&t);
    CHECK(settings_load(&t, path));
    CHECK(t.setup_done);
    CHECK_STR(t.music_dir, "/home/me/Music");
    CHECK_STR(t.video_dir, "C:\\Users\\me\\Videos = mine"); /* first '=' splits */
    CHECK_STR(t.section_dir[SECTION_ANIME], "/media/Anime");
    CHECK_STR(t.section_dir[SECTION_MOVIES], "");
    CHECK_NEAR(t.watched_threshold, 0.8);
    CHECK(t.theme == THEME_LIGHT);
    CHECK_NEAR(t.text_scale, 1.5);
    CHECK(t.high_contrast);
    CHECK(!t.video_thumbs);
    CHECK(!t.reduced_motion);
    CHECK_STR(t.hotkey[ACT_SEEK_BACK], "Left");
    CHECK_STR(t.hotkey[ACT_SEEK_FORWARD], "L");
    CHECK(settings_override(&t, "video:Avatar The Last Airbender") == SECTION_CARTOONS);
    CHECK(settings_override(&t, "video:Some Show") == SECTION_ANIME);
    CHECK(settings_override(&t, "video:Unknown") == -2);
    settings_set_override(&t, "video:Some Show", -2);
    CHECK(settings_override(&t, "video:Some Show") == -2);
    CHECK(t.noverrides == 1);

    /* Out-of-range values are clamped. */
    FILE *f = fopen(path, "w");
    fprintf(f, "watched_threshold = 3\ntext_scale = 0.1\nvolume = -5\n# comment\njunk line\n");
    fclose(f);
    settings_defaults(&t);
    CHECK(settings_load(&t, path));
    CHECK_NEAR(t.watched_threshold, 1.0);
    CHECK_NEAR(t.text_scale, 0.75);
    CHECK(t.volume == 0);
    /* 0.1.x: one remember_history switch becomes the three per-portal switches */
    f = fopen(path, "w");
    fprintf(f, "remember_history = 0\n");
    fclose(f);
    settings_defaults(&t);
    CHECK(settings_load(&t, path));
    CHECK(!t.history_video && !t.history_music && !t.history_books);
    CHECK(!settings_history_on(&t, KIND_MUSIC));
    f = fopen(path, "w");
    fprintf(f, "remember_history = 0\nhistory_music = 1\n");     /* the new keys win */
    fclose(f);
    settings_defaults(&t);
    CHECK(settings_load(&t, path));
    CHECK(t.history_music && t.history_video);
    /* the 0.2.0 settings round-trip, enums as words, ranges clamped */
    settings_defaults(&s);
    CHECK(s.play_threshold_music == 65 && s.music_resume_min_minutes == 10 && s.queue_auto_advance_video);
    CHECK(s.mediakeys_mode == KEYS_GLOBAL && !s.nowplaying_video && s.search_results_per_group == 5);
    s.history_books = false;
    s.queue_repeat = REPEAT_ALL;
    s.queue_video_in_music = VIDEO_IN_MUSIC_LISTEN;
    s.video_ontop = ONTOP_PLAYING;
    s.play_threshold_music = 80;
    s.private_session = true;                                  /* never saved */
    CHECK(settings_save(&s, path));
    settings_defaults(&t);
    CHECK(settings_load(&t, path));
    CHECK(!t.history_books && t.history_music);
    CHECK(t.queue_repeat == REPEAT_ALL && t.queue_video_in_music == VIDEO_IN_MUSIC_LISTEN);
    CHECK(t.video_ontop == ONTOP_PLAYING && t.play_threshold_music == 80);
    CHECK(!t.private_session && !settings_history_on(&t, KIND_BOOK));
    t.private_session = true;
    CHECK(!settings_history_on(&t, KIND_VIDEO));
    f = fopen(path, "w");
    fprintf(f, "play_threshold_music = 5\nsearch_results_per_group = 99\nqueue_repeat = bogus\n");
    fclose(f);
    settings_defaults(&t);
    CHECK(settings_load(&t, path));
    CHECK(t.play_threshold_music == 50 && t.search_results_per_group == 20 && t.queue_repeat == REPEAT_OFF);
    /* every action has a default hotkey, including the new ones */
    CHECK_STR(s.hotkey[ACT_SEARCH], "/");
    CHECK_STR(s.hotkey[ACT_QUEUE_PANEL], "Q");
    remove(path);
    /* settings compatibility: keys this THC does not know survive a load and save, in order (#26) */
    {
        const char *newer = "thc_test_newer.ini";
        FILE *nf = fopen(newer, "wb");
        CHECK(nf != NULL);
        if (nf) {
            fputs("# written by THC 9.9\n[library]\nvolume = 33\nfuture_flag = 1\n"
                  "section.holograms = /media/holo\nhotkey.teleport = Ctrl+Alt+T\n"
                  "nsfw_pin_hash = abc123\nweird key with spaces = some value = with equals\n", nf);
            fclose(nf);
        }
        Settings n;
        settings_defaults(&n);
        CHECK(settings_load(&n, newer));
        CHECK(n.volume == 33);                              /* known keys still apply */
        CHECK(strstr(n.unknown, "future_flag = 1\n"));
        CHECK(strstr(n.unknown, "section.holograms = /media/holo\n"));
        CHECK(strstr(n.unknown, "hotkey.teleport = Ctrl+Alt+T\n"));
        CHECK(strstr(n.unknown, "weird key with spaces = some value = with equals\n"));
        CHECK(!strstr(n.unknown, "volume"));                /* known keys are not "unknown" */
        n.volume = 44;                                      /* a known key changes... */
        CHECK(settings_save(&n, newer));
        Settings m;
        settings_defaults(&m);
        CHECK(settings_load(&m, newer));
        CHECK(m.volume == 44);
        CHECK(strcmp(m.unknown, n.unknown) == 0);           /* ...the unknown ones are untouched, same order */
        CHECK(strstr(m.unknown, "future_flag") < strstr(m.unknown, "hotkey.teleport"));
        CHECK(settings_save(&m, newer));                    /* a second round trip is stable */
        Settings k;
        settings_defaults(&k);
        CHECK(settings_load(&k, newer));
        CHECK(strcmp(k.unknown, n.unknown) == 0);
        /* a file with nothing unknown writes no extra section */
        Settings plain;
        settings_defaults(&plain);
        CHECK(settings_save(&plain, newer));
        FILE *rf = fopen(newer, "rb");
        char buf[16384] = "";
        if (rf) { size_t got = fread(buf, 1, sizeof buf - 1, rf); buf[got] = 0; fclose(rf); }
        CHECK(!strstr(buf, "kept from a newer THC"));
        /* a full buffer drops the surplus instead of overflowing */
        nf = fopen(newer, "wb");
        if (nf) {
            for (int i = 0; i < 2000; i++) fprintf(nf, "future_key_%d = %0100d\n", i, i);
            fclose(nf);
        }
        settings_defaults(&n);
        CHECK(settings_load(&n, newer));
        CHECK(strlen(n.unknown) < sizeof n.unknown && strstr(n.unknown, "future_key_0 "));
        remove(newer);
    }
    TEST_DONE();
}
