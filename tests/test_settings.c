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
    CHECK(s.remember_history);

    s.setup_done = true;
    snprintf(s.music_dir, sizeof s.music_dir, "/home/me/Music");
    snprintf(s.video_dir, sizeof s.video_dir, "C:\\Users\\me\\Videos = mine");
    snprintf(s.section_dir[SECTION_ANIME], sizeof s.section_dir[0], "/media/Anime");
    s.watched_threshold = 0.8;
    s.theme = THEME_LIGHT;
    s.text_scale = 1.5f;
    s.high_contrast = true;
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
    remove(path);
    TEST_DONE();
}
