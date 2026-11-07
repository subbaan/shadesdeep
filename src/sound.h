/* Sound through SDL3 audio: one-shot effects (with random variants, pitch,
 * pan and delay) and looping layers that fade in and out. If anything is
 * missing the game simply runs silent. */
#ifndef SOUND_H
#define SOUND_H

#include <stdbool.h>

#include "game.h"

typedef enum { LOOP_AMBIENCE, LOOP_TORCH, LOOP_FIRE, LOOP_WATER, LOOP_COUNT } LoopKind;

int  snd_init(const char *asset_dir);      /* 0 ok */
void snd_play(SoundKind k, float vol, float pitch, float pan, float delay);     /* pan -1..1 */
void snd_loop(LoopKind l, float gain);     /* where a loop's volume should glide to, 0..1 */
void snd_toggle_mute(void);
bool snd_muted(void);
bool snd_ready(void);
const char *snd_status(void);
void snd_shutdown(void);

#endif
