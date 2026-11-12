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
    TEST_DONE();
}
