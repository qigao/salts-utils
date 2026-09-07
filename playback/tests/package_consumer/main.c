#include <salts_playback.h>

int main(void) { return salts_playback_get_state(NULL) == SALTS_PLAYBACK_STATE_ERROR ? 0 : 1; }
