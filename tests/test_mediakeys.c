#include "mediakeys.h"
#include "test_util.h"

int main(void)
{
    Settings s;
    settings_defaults(&s);
    NowPlaying np;

    /* the keys are claimed only while something is loaded, and only when the mode allows it */
    CHECK(mediakeys_claim(KEYS_GLOBAL, true) && mediakeys_claim(KEYS_FOCUSED, true));
    CHECK(!mediakeys_claim(KEYS_GLOBAL, false));          /* nothing loaded: other players keep the keys */
    CHECK(!mediakeys_claim(KEYS_OFF, true));

    /* music: title, artist, album and cover are shared by default */
    mediakeys_build(&np, &s, KIND_MUSIC, true, true, "Halo", "Beyonc\xC3\xA9", "I Am", "/art/cover.jpg", 12, 200);
    CHECK(np.claimed && np.playing && !np.is_video);
    CHECK_STR(np.title, "Halo");
    CHECK_STR(np.artist, "Beyonc\xC3\xA9");
    CHECK_STR(np.album, "I Am");
    CHECK_STR(np.art, "/art/cover.jpg");
    CHECK_NEAR(np.position, 12);
    CHECK_NEAR(np.length, 200);

    /* audiobooks follow their own setting */
    mediakeys_build(&np, &s, KIND_BOOK, true, false, "Dune", "Frank Herbert", "", "", 0, 0);
    CHECK_STR(np.title, "Dune");
    s.nowplaying_books = false;
    mediakeys_build(&np, &s, KIND_BOOK, true, false, "Dune", "Frank Herbert", "", "", 0, 0);
    CHECK_STR(np.title, "THC");
    CHECK_STR(np.artist, "");
    s.nowplaying_books = true;

    /* VIDEO SHARES NOTHING by default: what you watch must not show on lock screens */
    mediakeys_build(&np, &s, KIND_VIDEO, true, true, "Secret Show S01E01", "Some Channel", "Season 1", "/art/poster.jpg", 5, 1400);
    CHECK(np.claimed && np.is_video);
    CHECK_STR(np.title, "THC");
    CHECK_STR(np.artist, "");
    CHECK_STR(np.album, "");
    CHECK_STR(np.art, "");
    CHECK(!strstr(np.title, "Secret"));
    /* ...unless the user turns it on */
    s.nowplaying_video = true;
    mediakeys_build(&np, &s, KIND_VIDEO, true, true, "Show", "x", "y", "/p.jpg", 5, 1400);
    CHECK_STR(np.title, "Show");
    s.nowplaying_video = false;

    /* "playing only" shares just the title; no cover without its setting */
    s.nowplaying_titles = TITLES_PLAYING_ONLY;
    mediakeys_build(&np, &s, KIND_MUSIC, true, true, "Halo", "Beyonc\xC3\xA9", "I Am", "/art/cover.jpg", 0, 0);
    CHECK_STR(np.title, "Halo");
    CHECK_STR(np.artist, "");
    CHECK_STR(np.album, "");
    s.nowplaying_titles = TITLES_FULL;
    s.nowplaying_art = false;
    mediakeys_build(&np, &s, KIND_MUSIC, true, true, "Halo", "B", "A", "/art/cover.jpg", 0, 0);
    CHECK_STR(np.art, "");
    s.nowplaying_art = true;

    /* nothing loaded: nothing is shared and the keys are released */
    mediakeys_build(&np, &s, KIND_MUSIC, false, false, "Halo", "B", "A", "/a.jpg", 0, 0);
    CHECK(!np.claimed && !np.title[0]);
    s.mediakeys_mode = KEYS_OFF;
    mediakeys_build(&np, &s, KIND_MUSIC, true, true, "Halo", "B", "A", "/a.jpg", 0, 0);
    CHECK(!np.claimed);

    /* long text is cut, not overflowed */
    char big[2000];
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = 0;
    s.mediakeys_mode = KEYS_GLOBAL;
    mediakeys_build(&np, &s, KIND_MUSIC, true, true, big, big, big, big, 0, 0);
    CHECK(strlen(np.title) == sizeof np.title - 1 && strlen(np.art) == sizeof np.art - 1);

    /* seek keys: 10 s for music and video, the audiobook skip length for audiobooks */
    CHECK_NEAR(mediakeys_seek_seconds(&s, KIND_MUSIC), 10);
    CHECK_NEAR(mediakeys_seek_seconds(&s, KIND_VIDEO), 10);
    CHECK_NEAR(mediakeys_seek_seconds(&s, KIND_BOOK), 30);
    s.skip_seconds = 45;
    s.mediakeys_seek_seconds = 20;
    CHECK_NEAR(mediakeys_seek_seconds(&s, KIND_BOOK), 45);
    CHECK_NEAR(mediakeys_seek_seconds(&s, KIND_MUSIC), 20);
    TEST_DONE();
}
