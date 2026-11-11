/* music.c: placeholder until the audio slice. */
#include "app.h"

void music_init(App *a) { (void)a; }
void music_shutdown(App *a) { (void)a; }
void music_pump(App *a) { (void)a; }
bool music_busy(App *a) { (void)a; return false; }
float music_bar_height(App *a) { (void)a; return 0; }
void music_bar(App *a) { (void)a; }
bool music_hotkey(App *a, Action act) { (void)a; (void)act; return false; }
void music_play_folder(App *a, int root, const FsNode *dir, int track_index)
{
    (void)root; (void)dir; (void)track_index;
    app_toast(a, "Music playback is coming in the next build");
}
bool music_is_current(App *a, const FsNode *track) { (void)a; (void)track; return false; }
