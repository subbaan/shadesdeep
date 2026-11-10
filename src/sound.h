/* Sound through SDL3 audio: one-shot effects (with random variants, pitch,
 * pan and delay), looping layers that fade in and out, and music (stereo Ogg
 * Vorbis streamed from memory, or WAV) that crossfades from piece to piece.
 * If anything is missing the game simply runs without it. */
#ifndef SOUND_H
#define SOUND_H

#include <stdbool.h>

#include "game.h"

typedef enum { LOOP_AMBIENCE, LOOP_TORCH, LOOP_FIRE, LOOP_WATER, LOOP_COUNT } LoopKind;

/* the title and the hall of fame loop; death and victory play once */
typedef enum { MUS_NONE, MUS_TITLE, MUS_FAME, MUS_DEATH, MUS_VICTORY, MUS_COUNT } Music;

int  snd_init(const char *asset_dir);      /* 0 ok */
void snd_play(SoundKind k, float vol, float pitch, float pan, float delay);     /* pan -1..1 */
void snd_loop(LoopKind l, float gain);     /* where a loop's volume should glide to, 0..1 */
/* fade over to this piece (MUS_NONE: fade the music out). A piece that had faded
 * out, or a play-once piece that had finished, starts again from the top. */
void snd_music(Music m);
void snd_toggle_music(void);               /* the music on or off (F3); sound effects stay */
bool snd_music_on(void);
void snd_toggle_mute(void);
bool snd_muted(void);
bool snd_ready(void);
const char *snd_status(void);
void snd_shutdown(void);

#endif
