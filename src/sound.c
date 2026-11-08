#include "sound.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE    48000
#define VOICES  32
#define CHUNK   512
#define MAX_VAR 6
#define SLEW    (1.0f / (RATE * 0.6f))      /* loops glide over about 0.6s */

typedef struct {
    float *data;        /* mono float at RATE */
    int len;
} Clip;

typedef struct {
    const Clip *clip;
    double pos, step;   /* pos < 0: still waiting (a delayed sound) */
    float gl, gr;
} Voice;

typedef struct {
    Clip clip;
    float vol;          /* from sounds.txt */
    double pos;
    float gain, target;
} Loop;

static const char *EVENT[SND_COUNT] = {
    "step", "wade", "swim", "splash", "swing", "hit", "hurt", "die", "kill",
    "rat", "goblin", "shade", "burn", "dissolve", "alert",
    "ignite", "sconce", "snuff", "hiss", "throw", "clink", "thud", "shatter",
    "drink", "pickup", "door", "stairs", "fall", "land", "click",
    "bat", "ogre", "eel", "sling",
};
static const char *LOOP_EVENT[LOOP_COUNT] = { "ambience", "torch_loop", "fire_loop", "water_loop" };

static Clip clips[SND_COUNT][MAX_VAR];
static int nvar[SND_COUNT];
static float evol[SND_COUNT];
static int last_var[SND_COUNT];
static Loop loops[LOOP_COUNT];
static Voice voices[VOICES];
static SDL_AudioStream *stream;
static SDL_Mutex *lock;
static bool ready, muted;
static char status[160] = "sound off";

static int load_clip(Clip *c, const char *dir, const char *file)
{
    char path[2048];
    snprintf(path, sizeof path, "%s/%s", dir, file);
    SDL_AudioSpec spec;
    Uint8 *buf = NULL, *out = NULL;
    Uint32 len = 0;
    int outlen = 0;
    if (!SDL_LoadWAV(path, &spec, &buf, &len)) return -1;
    SDL_AudioSpec dst = { SDL_AUDIO_F32, 1, RATE };
    bool ok = SDL_ConvertAudioSamples(&spec, buf, (int)len, &dst, &out, &outlen);
    SDL_free(buf);
    if (!ok) return -1;
    c->data = (float *)out;
    c->len = outlen / (int)sizeof(float);
    if (c->len < 2) { SDL_free(out); c->data = NULL; return -1; }
    return 0;
}

static void SDLCALL feed(void *ud, SDL_AudioStream *st, int additional, int total)
{
    (void)ud; (void)total;
    float mixbuf[CHUNK * 2];
    int frames = additional / (int)(2 * sizeof(float));
    while (frames > 0) {
        int n = frames < CHUNK ? frames : CHUNK;
        memset(mixbuf, 0, sizeof(float) * 2 * n);
        SDL_LockMutex(lock);
        for (int l = 0; l < LOOP_COUNT; l++) {
            Loop *lp = &loops[l];
            if (!lp->clip.data || (lp->gain <= 0 && lp->target <= 0)) continue;
            const float *d = lp->clip.data;
            int len = lp->clip.len;
            for (int i = 0; i < n; i++) {
                if (lp->gain < lp->target) lp->gain = fminf(lp->target, lp->gain + SLEW);
                else if (lp->gain > lp->target) lp->gain = fmaxf(lp->target, lp->gain - SLEW);
                int ip = (int)lp->pos;
                if (ip >= len) { lp->pos -= len; ip = 0; }
                float v = muted ? 0 : d[ip] * lp->gain * lp->vol;
                mixbuf[2 * i] += v * 0.7071f;
                mixbuf[2 * i + 1] += v * 0.7071f;
                lp->pos += 1;
            }
        }
        if (!muted)
            for (int v = 0; v < VOICES; v++) {
                Voice *vc = &voices[v];
                if (!vc->clip) continue;
                const float *d = vc->clip->data;
                int len = vc->clip->len;
                for (int i = 0; i < n; i++) {
                    if (vc->pos < 0) { vc->pos += 1; continue; }
                    int ip = (int)vc->pos;
                    if (ip >= len - 1) { vc->clip = NULL; break; }
                    float fr = (float)(vc->pos - ip);
                    float s = d[ip] + (d[ip + 1] - d[ip]) * fr;
                    mixbuf[2 * i] += s * vc->gl;
                    mixbuf[2 * i + 1] += s * vc->gr;
                    vc->pos += vc->step;
                }
            }
        else
            for (int v = 0; v < VOICES; v++) voices[v].clip = NULL;
        SDL_UnlockMutex(lock);
        for (int i = 0; i < 2 * n; i++) {     /* gentle soft clip */
            float x = mixbuf[i];
            mixbuf[i] = x / (1.0f + fabsf(x) * 0.25f);
        }
        SDL_PutAudioStreamData(st, mixbuf, n * 2 * (int)sizeof(float));
        frames -= n;
    }
}

/* "a.wav,b.wav,c.wav" */
static int load_variants(SoundKind k, const char *dir, char *files)
{
    int got = 0;
    for (char *f = strtok(files, ","); f && nvar[k] < MAX_VAR; f = strtok(NULL, ",")) {
        if (load_clip(&clips[k][nvar[k]], dir, f) == 0) { nvar[k]++; got++; }
    }
    return got;
}

int snd_init(const char *asset_dir)
{
    char dir[1536], path[2048];
    snprintf(dir, sizeof dir, "%s/sounds", asset_dir);
    snprintf(path, sizeof path, "%s/sounds.txt", dir);
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(status, sizeof status, "no %.120s", path); return -1; }

    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512");
    SDL_SetHint(SDL_HINT_APP_NAME, "Lume");
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        snprintf(status, sizeof status, "audio: %s", SDL_GetError());
        fclose(f);
        return -1;
    }
    lock = SDL_CreateMutex();

    int loaded = 0, missing = 0;
    char line[1024], ev[64], files[768];
    float vol;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || sscanf(line, "%63s %767s %f", ev, files, &vol) != 3) continue;
        bool known = false;
        for (int l = 0; l < LOOP_COUNT; l++)
            if (!strcmp(ev, LOOP_EVENT[l])) {
                known = true;
                char *comma = strchr(files, ',');
                if (comma) *comma = 0;
                if (loops[l].clip.data) SDL_free(loops[l].clip.data);
                memset(&loops[l], 0, sizeof loops[l]);
                if (load_clip(&loops[l].clip, dir, files) == 0) { loops[l].vol = vol; loaded++; }
                else missing++;
            }
        for (int k = 0; k < SND_COUNT && !known; k++)
            if (!strcmp(ev, EVENT[k])) {
                known = true;
                evol[k] = vol;
                int got = load_variants((SoundKind)k, dir, files);
                loaded += got;
                if (!got) missing++;
            }
    }
    fclose(f);

    SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, RATE };
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, NULL);
    if (!stream) {
        snprintf(status, sizeof status, "audio device: %s", SDL_GetError());
        return -1;
    }
    loops[LOOP_AMBIENCE].target = 1.0f;
    SDL_ResumeAudioStreamDevice(stream);
    ready = true;
    snprintf(status, sizeof status, "%d sounds%s", loaded, missing ? " (some missing)" : "");
    return 0;
}

void snd_play(SoundKind k, float vol, float pitch, float pan, float delay)
{
    if (!ready || muted || k < 0 || k >= SND_COUNT || !nvar[k]) return;
    /* a different variant from last time, when there's a choice */
    int v = nvar[k] > 1 ? rand() % nvar[k] : 0;
    if (nvar[k] > 1 && v == last_var[k]) v = (v + 1) % nvar[k];
    last_var[k] = v;
    const Clip *c = &clips[k][v];
    SDL_LockMutex(lock);
    int pick = -1;
    for (int i = 0; i < VOICES && pick < 0; i++)
        if (!voices[i].clip) pick = i;
    if (pick < 0) {         /* steal the one furthest through */
        double best = -1;
        for (int i = 0; i < VOICES; i++)
            if (voices[i].pos / voices[i].clip->len > best) {
                best = voices[i].pos / voices[i].clip->len;
                pick = i;
            }
    }
    if (pick >= 0) {
        float g = vol * evol[k];
        if (pan < -1) pan = -1;
        if (pan > 1) pan = 1;
        voices[pick] = (Voice){ c, -(double)(delay > 0 ? delay : 0) * RATE, pitch > 0.05f ? pitch : 1.0f,
                                g * sqrtf(0.5f * (1 - pan)), g * sqrtf(0.5f * (1 + pan)) };
    }
    SDL_UnlockMutex(lock);
}

void snd_loop(LoopKind l, float gain)
{
    if (!ready || l < 0 || l >= LOOP_COUNT) return;
    if (gain < 0) gain = 0;
    if (gain > 1) gain = 1;
    SDL_LockMutex(lock);
    loops[l].target = gain;
    SDL_UnlockMutex(lock);
}

void snd_toggle_mute(void)
{
    if (!ready) return;
    SDL_LockMutex(lock);
    muted = !muted;
    SDL_UnlockMutex(lock);
}

bool snd_muted(void) { return muted; }
bool snd_ready(void) { return ready; }

const char *snd_status(void)
{
    static char buf[200];
    snprintf(buf, sizeof buf, "%s%s", status, muted ? " (muted)" : "");
    return buf;
}

void snd_shutdown(void)
{
    if (stream) SDL_DestroyAudioStream(stream);
    stream = NULL;
    ready = false;
    for (int k = 0; k < SND_COUNT; k++)
        for (int v = 0; v < nvar[k]; v++) { SDL_free(clips[k][v].data); clips[k][v].data = NULL; }
    memset(nvar, 0, sizeof nvar);
    for (int l = 0; l < LOOP_COUNT; l++) { SDL_free(loops[l].clip.data); memset(&loops[l], 0, sizeof loops[l]); }
    memset(voices, 0, sizeof voices);
    if (lock) SDL_DestroyMutex(lock);
    lock = NULL;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
