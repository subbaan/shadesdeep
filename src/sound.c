#include "sound.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <vorbis/vorbisfile.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

/* ---------------------------------------------------------------- music */

static const char *MUSIC_EVENT[MUS_COUNT] = { "", "title_music", "fame_music", "death_music", "victory_music" };
static const bool MUSIC_ONCE[MUS_COUNT] = { false, false, false, true, true };
/* used when sounds.txt has no line for a piece */
static const struct { const char *file; float vol; } MUSIC_DEFAULT[MUS_COUNT] = {
    { NULL, 0 }, { "music_title.ogg", 0.45f }, { "music_fame.ogg", 0.45f },
    { "music_death.ogg", 0.50f }, { "music_victory.ogg", 0.50f },
};
#define MUS_FADE (1.0f / (RATE * 1.6f))     /* crossfades take about 1.6s */
#define TBUF 2048                           /* frames decoded at a time */

typedef struct { const unsigned char *p; size_t len, pos; } MemFile;

typedef struct {
    float vol;          /* from sounds.txt */
    bool once;
    /* a WAV: all of it, interleaved stereo at RATE */
    float *pcm;
    int frames;
    /* Ogg Vorbis: the compressed file in memory, decoded as it plays */
    unsigned char *ogg;
    MemFile mf;
    OggVorbis_File vf;
    bool vf_open;
    float buf[TBUF * 2];
    int buf_n, buf_i;
    double frac, step;  /* resampling: the file's rate over RATE */
    double pos;         /* WAV read position */
    float gain, target;
    bool restart, ended;
} Track;

static Track tracks[MUS_COUNT];
static bool music_off;

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

static size_t mem_read(void *ptr, size_t size, size_t n, void *ds)
{
    MemFile *m = ds;
    if (!size) return 0;
    size_t want = size * n, left = m->len - m->pos;
    if (want > left) want = left - left % size;
    memcpy(ptr, m->p + m->pos, want);
    m->pos += want;
    return want / size;
}

static int mem_seek(void *ds, ogg_int64_t off, int whence)
{
    MemFile *m = ds;
    ogg_int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (ogg_int64_t)m->pos : (ogg_int64_t)m->len;
    ogg_int64_t to = base + off;
    if (to < 0 || to > (ogg_int64_t)m->len) return -1;
    m->pos = (size_t)to;
    return 0;
}

static long mem_tell(void *ds) { return (long)((MemFile *)ds)->pos; }

static void track_free(Track *t)
{
    if (t->vf_open) ov_clear(&t->vf);
    SDL_free(t->pcm);
    free(t->ogg);
    memset(t, 0, sizeof *t);
}

static bool ends_with(const char *s, const char *tail)
{
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && !strcasecmp(s + a - b, tail);
}

/* a piece of music: .ogg is kept compressed and streamed, anything else goes through SDL_LoadWAV */
static int load_track_file(Track *t, const char *dir, const char *file)
{
    char path[2048];
    snprintf(path, sizeof path, "%s/%s", dir, file);
    if (ends_with(file, ".ogg")) {
        FILE *f = fopen(path, "rb");
        if (!f) return -1;
        fseek(f, 0, SEEK_END);
        long len = ftell(f);
        fseek(f, 0, SEEK_SET);
        unsigned char *data = len > 0 ? malloc((size_t)len) : NULL;
        bool ok = data && fread(data, 1, (size_t)len, f) == (size_t)len;
        fclose(f);
        if (!ok) { free(data); return -1; }
        t->ogg = data;
        t->mf = (MemFile){ data, (size_t)len, 0 };
        ov_callbacks cb = { mem_read, mem_seek, NULL, mem_tell };
        if (ov_open_callbacks(&t->mf, &t->vf, NULL, 0, cb) != 0) { free(data); t->ogg = NULL; return -1; }
        t->vf_open = true;
        vorbis_info *vi = ov_info(&t->vf, -1);
        if (!vi || vi->channels < 1 || vi->rate < 8000) { track_free(t); return -1; }
        t->step = (double)vi->rate / RATE;
        return 0;
    }
    SDL_AudioSpec spec;
    Uint8 *buf = NULL, *out = NULL;
    Uint32 len = 0;
    int outlen = 0;
    if (!SDL_LoadWAV(path, &spec, &buf, &len)) return -1;
    SDL_AudioSpec dst = { SDL_AUDIO_F32, 2, RATE };
    bool ok = SDL_ConvertAudioSamples(&spec, buf, (int)len, &dst, &out, &outlen);
    SDL_free(buf);
    if (!ok) return -1;
    t->pcm = (float *)out;
    t->frames = outlen / (int)(2 * sizeof(float));
    if (t->frames < 2) { track_free(t); return -1; }
    return 0;
}

/* the named file, or the same piece in the other format if that's what there is */
static int load_track(Track *t, const char *dir, const char *file, float vol, bool once)
{
    track_free(t);
    int rc = load_track_file(t, dir, file);
    if (rc != 0) {
        char alt[512];
        snprintf(alt, sizeof alt, "%s", file);
        char *dot = strrchr(alt, '.');
        if (dot && (size_t)(dot - alt) + 5 < sizeof alt) {
            strcpy(dot, ends_with(file, ".ogg") ? ".wav" : ".ogg");
            rc = load_track_file(t, dir, alt);
        }
    }
    if (rc == 0) { t->vol = vol; t->once = once; }
    return rc;
}

/* decode the next stretch of an Ogg track, keeping the frame we're on as the first */
static void track_fill(Track *t)
{
    int n = 0;
    if (t->buf_n > 0) {
        t->buf[0] = t->buf[2 * (t->buf_n - 1)];
        t->buf[1] = t->buf[2 * (t->buf_n - 1) + 1];
        n = 1;
    }
    int empty = 0;
    while (n < TBUF && !t->ended) {
        float **pcm;
        int bs;
        long got = ov_read_float(&t->vf, &pcm, TBUF - n, &bs);
        if (got == OV_HOLE) continue;
        if (got <= 0) {
            if (t->once || ++empty > 2 || ov_pcm_seek(&t->vf, 0) != 0) {
                if (n <= 1) t->ended = true;
                break;
            }
            continue;
        }
        empty = 0;
        int ch = ov_info(&t->vf, -1)->channels;
        for (long i = 0; i < got; i++) {
            t->buf[2 * (n + i)] = pcm[0][i];
            t->buf[2 * (n + i) + 1] = pcm[ch > 1 ? 1 : 0][i];
        }
        n += (int)got;
    }
    t->buf_n = n;
    t->buf_i = 0;
}

static void track_rewind(Track *t)
{
    t->ended = false;
    t->restart = false;
    t->pos = 0;
    t->frac = 0;
    t->buf_n = t->buf_i = 0;
    if (t->vf_open) ov_pcm_seek(&t->vf, 0);
}

/* the next output frame of a track, false when a play-once piece has finished */
static bool track_frame(Track *t, float *l, float *r)
{
    if (t->ended) return false;
    if (t->pcm) {
        int ip = (int)t->pos;
        if (ip >= t->frames) {
            if (t->once) { t->ended = true; return false; }
            t->pos = 0;
            ip = 0;
        }
        *l = t->pcm[2 * ip];
        *r = t->pcm[2 * ip + 1];
        t->pos += 1;
        return true;
    }
    if (!t->vf_open) return false;
    if (t->buf_i + 1 >= t->buf_n) {
        track_fill(t);
        if (t->buf_n < 2) { t->ended = true; return false; }
    }
    const float *a = &t->buf[2 * t->buf_i], *b = a + 2;
    float fr = (float)t->frac;
    *l = a[0] + (b[0] - a[0]) * fr;
    *r = a[1] + (b[1] - a[1]) * fr;
    t->frac += t->step;
    while (t->frac >= 1.0) {
        t->frac -= 1.0;
        t->buf_i++;
        if (t->buf_i + 1 >= t->buf_n) {
            track_fill(t);
            if (t->buf_n < 2) { t->ended = true; break; }
        }
    }
    return true;
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
        for (int m = 1; m < MUS_COUNT; m++) {
            Track *tr = &tracks[m];
            if (!tr->pcm && !tr->vf_open) continue;
            if (tr->restart) track_rewind(tr);
            if (tr->gain <= 0 && tr->target <= 0) continue;
            for (int i = 0; i < n; i++) {
                if (tr->gain < tr->target) tr->gain = fminf(tr->target, tr->gain + MUS_FADE);
                else if (tr->gain > tr->target) tr->gain = fmaxf(tr->target, tr->gain - MUS_FADE);
                float l, r;
                if (!track_frame(tr, &l, &r)) break;
                float g = muted || music_off ? 0 : tr->gain * tr->vol;
                mixbuf[2 * i] += l * g;
                mixbuf[2 * i + 1] += r * g;
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
    SDL_SetHint(SDL_HINT_APP_NAME, "Shadesdeep");
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
        for (int m = 1; m < MUS_COUNT && !known; m++)
            if (!strcmp(ev, MUSIC_EVENT[m])) {
                known = true;
                char *comma = strchr(files, ',');
                if (comma) *comma = 0;
                if (load_track(&tracks[m], dir, files, vol, MUSIC_ONCE[m]) == 0) loaded++;
                else missing++;
            }
        for (int l = 0; l < LOOP_COUNT && !known; l++)
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
    for (int m = 1; m < MUS_COUNT; m++)     /* pieces sounds.txt doesn't mention */
        if (!tracks[m].pcm && !tracks[m].vf_open &&
            load_track(&tracks[m], dir, MUSIC_DEFAULT[m].file, MUSIC_DEFAULT[m].vol, MUSIC_ONCE[m]) == 0) loaded++;

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

void snd_music(Music m)
{
    if (!ready || m < 0 || m >= MUS_COUNT) return;
    SDL_LockMutex(lock);
    for (int i = 1; i < MUS_COUNT; i++) {
        Track *t = &tracks[i];
        if (i == (int)m) {
            if (t->gain <= 0 || t->ended) {
                t->restart = true;         /* the feed rewinds it, off this thread's clock */
                t->ended = false;
                if (t->once) t->gain = 1.0f;    /* a piece that plays once opens at full strength */
            }
            t->target = 1.0f;
        } else {
            t->target = 0.0f;
        }
    }
    SDL_UnlockMutex(lock);
}

void snd_toggle_music(void)
{
    if (!ready) return;
    SDL_LockMutex(lock);
    music_off = !music_off;
    SDL_UnlockMutex(lock);
}

bool snd_music_on(void) { return !music_off; }

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
    snprintf(buf, sizeof buf, "%s%s%s", status, muted ? " (muted)" : "", music_off && !muted ? " (music off)" : "");
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
    for (int m = 0; m < MUS_COUNT; m++) track_free(&tracks[m]);
    memset(voices, 0, sizeof voices);
    if (lock) SDL_DestroyMutex(lock);
    lock = NULL;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
