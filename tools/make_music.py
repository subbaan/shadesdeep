#!/usr/bin/env python3
"""Compose and synthesize Shadesdeep's music.

Four pieces, all made from oscillators and noise (no samples), mixed in stereo
in a big stone hall:

  music_title    "Into the Deep"        harp, flute, choir and frame drum; loops
  music_fame     "The Hall of the Fallen" a slow sarabande for viol, flute, harp; loops
  music_death    "Lament"               a tolling bell and a viol's falling line; plays once
  music_victory  "The Heartflame"       timpani, brass fanfare, bells and a hymn; plays once

Each is written as Ogg Vorbis through ffmpeg (or WAV with --wav, or when ffmpeg
is missing). Existing files are kept unless named with --only or with --force,
so music you've swapped in survives a re-run.

Run: python3 tools/make_music.py [out_dir]            (default assets/sounds)
     python3 tools/make_music.py --only title death   render just these
     python3 tools/make_music.py --force              re-render all
     python3 tools/make_music.py --wav                write .wav instead of .ogg
"""
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, lfilter, sosfilt, fftconvolve

VERSION = "1.0.0"
SR = 48000          # the game mixes at 48 kHz, so the music needs no resampling


# ------------------------------------------------------------ primitives

def n_(d):
    return max(1, int(round(SR * d)))


def tt(d):
    return np.arange(n_(d)) / SR


def midi(m):
    return 440.0 * 2 ** ((m - 69) / 12)


def lp(x, hz, order=2):
    return sosfilt(butter(order, min(hz / (SR / 2), 0.99), output="sos"), x)


def hp(x, hz, order=2):
    return sosfilt(butter(order, max(hz / (SR / 2), 0.0002), "high", output="sos"), x)


def bp(x, lo, hi, order=2):
    return sosfilt(butter(order, [max(lo / (SR / 2), 0.0002), min(hi / (SR / 2), 0.99)], "band", output="sos"), x)


def peak(x, hz, q=1.2):
    """A resonant band around hz (for bodies and formants)."""
    bw = hz / q
    return bp(x, hz - bw / 2, hz + bw / 2, 1)


def adsr(n, a, d, s, r, sr=SR):
    """n samples of note (the release is added after), times in seconds."""
    na, nd, nr = max(1, int(a * sr)), max(1, int(d * sr)), max(1, int(r * sr))
    e = np.empty(n + nr)
    k = np.arange(n)
    e[:n] = np.where(k < na, k / na, s + (1 - s) * np.exp(-(k - na) / nd))
    last = e[n - 1] if n else 0
    e[n:] = last * np.linspace(1, 0, nr) ** 1.5
    return e


def blep_saw(freq):
    """Band-limited sawtooth (polyBLEP) following a per-sample frequency array."""
    dt = np.asarray(freq) / SR
    p = np.cumsum(dt) % 1.0
    y = 2 * p - 1
    m = p < dt
    t = p[m] / dt[m]
    y[m] -= t + t - t * t - 1
    m = p > 1 - dt
    t = (p[m] - 1) / dt[m]
    y[m] -= t * t + t + t + 1
    return y


def tv_lp(x, cut, block=128):
    """Low-pass whose cutoff follows the array cut (block-wise, filter state carried)."""
    y = np.empty_like(x)
    zi = np.zeros(2)
    for i in range(0, len(x), block):
        c = float(np.clip(cut[min(i, len(cut) - 1)], 30, SR * 0.45))
        b, a = butter(1, c / (SR / 2))
        b = np.convolve(b, b)
        a = np.convolve(a, a)
        y[i:i + block], zi = lfilter(b, a, x[i:i + block], zi=zi)
    return y


def ffill(x, default):
    """Hold the last value over NaNs (and use default before the first)."""
    idx = np.where(~np.isnan(x), np.arange(len(x)), 0)
    np.maximum.accumulate(idx, out=idx)
    y = x[idx]
    y[np.isnan(y)] = default
    return y


# ------------------------------------------------------------ the mixer

class Mix:
    """A stereo bus with a reverb send. add() places a mono part at a time."""

    def __init__(self, dur):
        self.n = n_(dur)
        self.dry = np.zeros((2, self.n))
        self.send = np.zeros((2, self.n))

    def add(self, x, t0, pan=0.0, gain=1.0, rev=0.3):
        i = n_(t0) if t0 > 0 else 0
        j = min(self.n, i + len(x))
        if j <= i:
            return
        x = np.asarray(x[:j - i]) * gain
        a = (pan + 1) * np.pi / 4
        for c, g in ((0, np.cos(a)), (1, np.sin(a))):
            self.dry[c, i:j] += x * g
            self.send[c, i:j] += x * g * rev

    def render(self, ir):
        out = self.dry.copy()
        for c in (0, 1):
            w = fftconvolve(self.send[c], ir[c])[:self.n]
            o = fftconvolve(self.send[1 - c], ir[1 - c])[:self.n]
            out[c] += w * 0.85 + o * 0.15
        return out


def hall_ir(rt60=2.8, length=3.6, predelay=0.024, seed=7, dark=1.0):
    """A big stone hall: decorrelated noise tails, highs dying fastest."""
    rng = np.random.default_rng(seed)
    n = n_(length)
    t = np.arange(n) / SR
    ir = np.zeros((2, n + n_(predelay)))
    for c in (0, 1):
        z = rng.normal(size=n)
        lo, mid, hi = lp(z, 400), bp(z, 400, 2600), hp(z, 2600)
        dec = lambda rt: np.exp(-6.91 * t / rt)
        x = lo * dec(rt60 * 1.2) + mid * dec(rt60) * 0.8 + hi * dec(rt60 * 0.4 / dark) * 0.45
        x *= 1 - np.exp(-t / 0.015)
        # early reflections off the near walls
        for ms, g in ((9, 0.5), (17, 0.38), (26, 0.3), (41, 0.22), (57, 0.16)):
            k = n_((ms + rng.uniform(-3, 3)) / 1000)
            x[k] += g * 9 * (1 if rng.uniform() < 0.5 else -1)
        ir[c, n_(predelay):] = lp(x, 7000)
    return ir / np.sqrt(np.sum(ir ** 2) / 2)


def master(st, loop_n=None, target=0.17):
    """Even out the loudness across the pieces, then catch the peaks gently."""
    if loop_n is not None:
        tail = st.shape[1] - loop_n
        st[:, :tail] += st[:, loop_n:]
        st = st[:, :loop_n]
    st = np.stack([hp(st[0], 28), hp(st[1], 28)])
    mono = st.mean(axis=0)
    w = n_(0.4)
    k = len(mono) // w
    rms = np.sqrt(np.mean(mono[:k * w].reshape(k, w) ** 2, axis=1))
    loud = np.percentile(rms, 90) or 1
    st *= target / loud
    knee = 0.72
    over = np.abs(st) > knee
    st[over] = np.sign(st[over]) * (knee + (1 - knee) * np.tanh((np.abs(st[over]) - knee) / (1 - knee)))
    return st * 0.97


def shape(st, points):
    """Multiply by a gain curve through (time s, gain) points: the piece's dynamics."""
    t = np.arange(st.shape[1]) / SR
    pt, pg = zip(*points)
    return st * np.interp(t, pt, pg)


def crash(d=2.5, seed=0):
    """A struck cymbal, for landing on after a swell."""
    rng = np.random.default_rng(seed)
    t = tt(d)
    x = hp(rng.normal(size=len(t)), 3500) * np.exp(-t / 0.7)
    x += bp(rng.normal(size=len(t)), 600, 3000) * np.exp(-t / 0.08) * 0.6
    na = n_(0.002)
    x[:na] *= np.linspace(0, 1, na)
    return x


# ------------------------------------------------------------ instruments

_PLUCKS = {}


def pluck(m, d=2.6, pos=0.28, decay=2.4, bright=0.6, var=0):
    """A plucked string (harp, lute) by additive partials, each dying at its own rate."""
    key = (m, d, pos, decay, bright, var)
    if key in _PLUCKS:
        return _PLUCKS[key]
    rng = np.random.default_rng(int(m * 97 + var * 13 + pos * 1000))
    f = midi(m)
    t = tt(d)
    out = np.zeros_like(t)
    kmax = int(min(26, 9500 / f))
    for k in range(1, kmax + 1):
        fk = f * k * np.sqrt(1 + 0.0001 * k * k)
        a = abs(np.sin(np.pi * k * pos)) / k ** (1.3 - 0.4 * bright)
        tau = decay / (1 + 0.55 * (k - 1) * (f / 300) ** 0.5)
        tau *= (220 / f) ** 0.25
        out += a * np.sin(2 * np.pi * fk * t + rng.uniform(0, 0.3)) * np.exp(-t / tau)
    nz = n_(0.008)
    tr = rng.normal(size=nz) * np.exp(-np.arange(nz) / (0.0012 * SR))
    out[:nz] += lp(tr, min(9000, f * 10)) * 0.25
    na = n_(0.002)
    out[:na] *= np.linspace(0, 1, na)
    nr = n_(0.08)
    out[-nr:] *= np.linspace(1, 0, nr)
    _PLUCKS[key] = out
    return out


def harp(m, d=2.8, var=0):
    return pluck(m, d, pos=0.3, decay=2.6, bright=0.55, var=var)


def legato(notes, total, harm, a=0.08, r=0.3, vib=0.01, vib_rate=5.2, glide=0.045, breath=0.0,
           swell=0.15, seed=1, body=None, darken=0.12):
    """One expressive voice (flute, viol, horn) playing notes in a line: the pitch glides
    from note to note, vibrato grows in as a note is held, louder is brighter.
    notes: (start s, length s, midi, velocity)."""
    rng = np.random.default_rng(seed)
    n = n_(total)
    logf = np.full(n, np.nan)
    amp = np.zeros(n)
    since = np.full(n, 10.0)
    t = np.arange(n) / SR
    for t0, d, m, v in notes:
        i, j = n_(t0), min(n, n_(t0 + d))
        if j <= i:
            continue
        logf[i:j] = np.log(midi(m))
        since[i:j] = np.arange(j - i) / SR
        e = adsr(j - i, a, 0.6, 1.0, r)
        k = np.arange(len(e)) / SR
        e *= 1 + swell * np.sin(np.pi * np.clip(k / max(d, 0.05), 0, 1))     # a bowed swell
        e *= 1 - 0.3 * np.exp(-k / 0.03) * (k > 0)                              # re-articulation
        jj = min(n, i + len(e))
        amp[i:jj] = np.maximum(amp[i:jj], v * e[:jj - i])
    first = np.nanmin(logf) if np.any(~np.isnan(logf)) else np.log(440)
    logf = ffill(logf, first)
    g = np.exp(-1 / (glide * SR))
    logf = lfilter([1 - g], [1, -g], logf, zi=[logf[0] * g])[0]
    wander = np.cumsum(rng.normal(0, 1, n)) / SR
    wander = lp(wander - wander.mean(), 0.5, 1)
    depth = vib * np.clip((since - 0.18) / 0.4, 0, 1)
    f = np.exp(logf) * (1 + depth * np.sin(2 * np.pi * vib_rate * t + 3 * wander))
    phi = np.cumsum(f) / SR
    out = np.zeros(n)
    amp_c = np.clip(amp, 0, 1.5)
    for k, h in enumerate(harm, 1):
        if h == 0:
            continue
        ok = (f * k) < 16000
        out += h * ok * amp_c ** (1 + darken * (k - 1)) * np.sin(2 * np.pi * k * phi + rng.uniform(0, 6.28))
    if body:
        out = sum(gn * peak(out, hz, q) for hz, q, gn in body) + out * 0.25
    if breath:
        z = rng.normal(size=n)
        chiff = np.zeros(n)
        for t0, d, m, v in notes:
            i = n_(t0)
            k = n_(0.05)
            j = min(n, i + k)
            chiff[i:j] += v * np.exp(-np.arange(j - i) / (0.012 * SR))
        out += breath * bp(z, 1800, 7000) * (amp_c * 0.6 + chiff * 2.5)
    return out


FLUTE = dict(harm=[1, 0.22, 0.09, 0.04, 0.02, 0.01], a=0.07, r=0.22, vib=0.007, vib_rate=5.0,
             breath=0.06, swell=0.1, darken=0.25)
VIOL_HARM = [1 / k for k in range(1, 28)]
VIOL = dict(harm=VIOL_HARM, a=0.14, r=0.35, vib=0.011, vib_rate=5.4, swell=0.25, darken=0.06,
            body=((280, 1.5, 1.0), (900, 1.8, 0.6), (2500, 2.0, 0.35)))
HORN = dict(harm=[1, 0.6, 0.38, 0.24, 0.14, 0.08, 0.05, 0.03], a=0.06, r=0.25, vib=0.004, vib_rate=4.8,
            swell=0.2, darken=0.3)


def viol(notes, total, seed=3, gain=1.0):
    x = legato(notes, total, seed=seed, **VIOL)
    return lp(x, 5200) * gain


def pad(chord, d, kind="strings", seed=0, a=0.6, r=1.2, cutoff=1900, voices=3, vowel="ah"):
    """A held chord: a string section, or a choir singing a vowel."""
    rng = np.random.default_rng(seed)
    n = n_(d)
    e = adsr(n, a, 1.0, 1.0, r)
    n = len(e)              # the tone keeps sounding through the release
    t = np.arange(n) / SR
    out = np.zeros(n)
    for m in chord:
        for v in range(voices):
            det = (v - (voices - 1) / 2) * (0.006 if kind == "strings" else 0.004) + rng.uniform(-0.002, 0.002)
            rate = rng.uniform(4.3, 5.8)
            vibd = 0.0035 if kind == "strings" else 0.005
            f = midi(m) * (1 + det) * (1 + vibd * np.sin(2 * np.pi * rate * t + rng.uniform(0, 6.28)))
            out += blep_saw(f) * rng.uniform(0.8, 1.0)
    out /= max(1, len(chord) * voices) ** 0.6
    if kind == "strings":
        out = lp(out, cutoff, 2) + 0.4 * peak(out, 1200, 1.0)
    else:
        F = {"ah": ((760, 1.0), (1150, 0.55), (2600, 0.25)),
             "oo": ((380, 1.0), (850, 0.35), (2500, 0.08)),
             "oh": ((520, 1.0), (880, 0.5), (2500, 0.12))}[vowel]
        out = sum(g * peak(out, hz, 3.2) for hz, g in F) * 2.4
        out += 0.02 * bp(rng.normal(size=n), 2000, 6000)
    return out * e


def brass(m, d, vel=1.0, seed=0):
    """A natural trumpet / horn: the tone opens up as it speaks, with a little scoop."""
    rng = np.random.default_rng(seed + m)
    n = n_(d)
    nr = n_(0.18)
    t = np.arange(n + nr) / SR
    scoop = 1 - 0.018 * np.exp(-t / 0.035)
    f = midi(m) * scoop * (1 + 0.003 * np.sin(2 * np.pi * 5 * t + rng.uniform(0, 6)))
    x = blep_saw(f) * 0.7 + blep_saw(f * 1.003) * 0.3
    e = adsr(n, 0.03, 0.25, 0.75, 0.18)
    cut = 500 + (2600 + 1800 * vel) * np.clip(e, 0, 1) ** 1.6
    y = tv_lp(x, cut)
    return y * e * vel


def timpani(m, vel=1.0, d=3.0, seed=0):
    rng = np.random.default_rng(seed + m * 7)
    f = midi(m)
    t = tt(d)
    drop = 1 + 0.03 * np.exp(-t / 0.05)
    out = np.zeros_like(t)
    for ratio, a, tau in ((1.0, 1.0, 1.4), (1.504, 0.5, 0.9), (1.742, 0.35, 0.7), (2.0, 0.28, 0.6),
                          (2.245, 0.18, 0.45), (2.494, 0.12, 0.35)):
        out += a * np.sin(2 * np.pi * f * ratio * np.cumsum(drop) / SR) * np.exp(-t / (tau * (0.7 + 0.3 * vel)))
    thump = lp(rng.normal(size=len(t)), 500) * np.exp(-t / 0.03) * 0.6
    out += thump
    na = n_(0.003)
    out[:na] *= np.linspace(0, 1, na)
    return lp(out, 1800 + 2500 * vel) * vel


def timp_roll(m, t0, d, v0, v1, mix, pan=0.0, gain=1.0, rev=0.35, seed=0):
    rng = np.random.default_rng(seed)
    t = 0.0
    while t < d:
        v = v0 + (v1 - v0) * (t / d) ** 1.4
        mix.add(timpani(m, v * rng.uniform(0.75, 1.0), 1.6, seed=int(t * 100)) * 0.5, t0 + t, pan, gain, rev)
        t += 1 / 14 * rng.uniform(0.85, 1.15)


def frame_drum(vel=1.0, low=True, seed=0):
    rng = np.random.default_rng(seed)
    d = 0.6 if low else 0.25
    t = tt(d)
    f0 = 92 if low else 180
    f = f0 * (1 + 0.6 * np.exp(-t / 0.02))
    body = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t / (0.18 if low else 0.06))
    skin = bp(rng.normal(size=len(t)), 300 if low else 900, 2500 if low else 5000) * np.exp(-t / (0.03 if low else 0.02))
    x = body * (1.0 if low else 0.35) + skin * (0.35 if low else 0.6)
    na = n_(0.002)
    x[:na] *= np.linspace(0, 1, na)
    return x * vel


_BELLS = {}


def bell(m, d=7.0, kind="church"):
    """Church bell: hum, prime, minor-third tierce, quint, nominal... each fading at its own rate.
    'hand': a small bright handbell, for pealing."""
    key = (m, d, kind)
    if key in _BELLS:
        return _BELLS[key]
    f = midi(m)
    t = tt(d)
    if kind == "church":
        parts = ((0.5, 0.55, 5.5), (1.0, 0.8, 3.6), (1.19, 0.6, 2.4), (1.5, 0.3, 1.8), (2.0, 0.65, 1.6),
                 (2.5, 0.25, 1.0), (3.0, 0.2, 0.8), (4.07, 0.12, 0.5), (5.2, 0.06, 0.3))
    else:
        parts = ((1.0, 1.0, 1.8), (2.0, 0.25, 1.0), (2.76, 0.35, 0.6), (5.4, 0.18, 0.25), (8.9, 0.08, 0.12))
    out = np.zeros_like(t)
    for i, (r, a, tau) in enumerate(parts):
        fr = f * r
        if fr > 16000:
            continue
        beat = 1 + 0.25 * np.sin(2 * np.pi * (0.7 + i * 0.37) * t)    # the slow warble of a real bell
        out += a * np.sin(2 * np.pi * fr * t + i) * np.exp(-t / tau) * beat
    k = n_(0.004)
    strike = np.random.default_rng(m).normal(size=k) * np.exp(-np.arange(k) / (0.001 * SR))
    out[:k] += hp(strike, 2000) * 0.3
    na = n_(0.001)
    out[:na] *= np.linspace(0, 1, na)
    _BELLS[key] = out
    return out


def wind(d, seed=0):
    rng = np.random.default_rng(seed)
    n = n_(d)
    x = np.cumsum(rng.normal(size=n))
    x = hp(x, 40)
    t = np.arange(n) / SR
    x = bp(x, 150, 900) * (0.6 + 0.4 * np.sin(2 * np.pi * t / 7.3) * np.sin(2 * np.pi * t / 3.1 + 1))
    return x / (np.max(np.abs(x)) or 1)


# ------------------------------------------------------------ helpers for writing music

def line(bar0, mel, unit, bar_units, vel=0.8, legato_k=1.0):
    """[(midi|None, units)] starting at bar0 -> notes (start, length, midi, vel)."""
    out = []
    s = bar0 * bar_units
    for m, ln in mel:
        if m is not None:
            v = vel[0] if isinstance(vel, tuple) else vel
            out.append((s * unit, ln * unit * legato_k, m, v))
        s += ln
    return out


def arp_harp(mix, t0, chord_low, pattern, step, pan=-0.25, gain=1.0, rev=0.35, var=0, vel=None):
    """Broken chord on the harp: pattern indexes into chord_low (a list of midi notes)."""
    for k, idx in enumerate(pattern):
        if idx is None:
            continue
        m = chord_low[idx]
        v = (vel[k] if vel else 1.0)
        mix.add(harp(m, 3.0, var=(k + var) % 2) * v, t0 + k * step, pan + 0.05 * (idx - 2), gain, rev)


def chord_notes(root, quality):
    q = {"m": (0, 3, 7), "M": (0, 4, 7), "sus4": (0, 5, 7), "7": (0, 4, 7, 10), "m7": (0, 3, 7, 10),
         "dim": (0, 3, 6)}[quality]
    return [root + i for i in q]


# ------------------------------------------------------------ 1. title: "Into the Deep"
# D minor, 6/8, a dotted-quarter pulse of 56. Harp over a low drone; a flute takes the tune,
# then the strings lift it and a choir and frame drum carry it to the top before it settles.

T_CH = {   # chord: (bass root, harp voicing low->high)
    "Dm": (38, [50, 57, 62, 65, 69, 74]), "C": (36, [48, 55, 60, 64, 67, 72]),
    "Bb": (34, [46, 53, 58, 62, 65, 70]), "A": (33, [45, 52, 57, 61, 64, 69]),
    "Gm": (31, [43, 50, 55, 58, 62, 67]), "F": (29, [41, 48, 53, 57, 60, 65]),
    "Asus": (33, [45, 52, 57, 62, 64, 69]),
}
T_FORM = (["Dm", "Dm", "C", "Dm"],                                   # intro: harp and drone
          ["Dm", "C", "Bb", "A", "Dm", "C", "Gm", "A"],               # A: the flute
          ["Dm", "C", "Bb", "A", "Dm", "C", "Gm", "A"],               # A': the flute, answered
          ["Bb", "F", "C", "Dm", "Bb", "F", "Gm", "Asus"],            # B: the strings lift
          ["Dm", "Bb", "C", "A", "Dm", "Bb", "A", "Dm"],              # C: everyone
          ["Dm", "C", "Dm", "Dm"])                                   # outro: back to the harp
T_MEL_A = [(69, 3), (74, 2), (76, 1), (77, 3), (76, 2), (72, 1), (74, 4), (72, 1), (70, 1), (69, 6),
           (69, 2), (74, 1), (77, 3), (79, 3), (77, 2), (76, 1), (74, 2), (70, 1), (74, 2), (76, 1), (73, 3), (69, 3)]
T_MEL_A2 = [(69, 3), (74, 2), (76, 1), (77, 3), (79, 2), (77, 1), (76, 3), (74, 2), (72, 1), (73, 4), (76, 2),
            (77, 3), (76, 2), (74, 1), (72, 3), (74, 2), (76, 1), (74, 2), (70, 1), (67, 3), (69, 6)]
T_MEL_B = [(74, 2), (77, 1), (81, 3), (81, 3), (79, 2), (77, 1), (79, 4), (76, 2), (77, 3), (74, 3),
           (74, 2), (77, 1), (82, 3), (81, 3), (77, 2), (81, 1), (79, 3), (82, 2), (79, 1), (81, 6)]
T_MEL_C = [(74, 3), (77, 2), (81, 1), (82, 4), (81, 1), (79, 1), (79, 3), (76, 2), (72, 1), (73, 3), (76, 3),
           (77, 3), (81, 2), (77, 1), (74, 3), (77, 2), (74, 1), (76, 3), (73, 3), (74, 6)]
T_MEL_OUT = [(69, 3), (74, 3), (72, 6), (None, 6), (None, 6)]


def music_title():
    e8 = 60.0 / 168          # one eighth note
    bar = 6 * e8
    bars = [c for part in T_FORM for c in part]
    nb = len(bars)
    loop = nb * bar
    tail = 5.0
    mix = Mix(loop + tail)
    total = loop + tail

    # the drone: a low open fifth, breathing in and out, out of the way of the lift. Made
    # exactly periodic over the loop (whole cycles, filtered over two laps) so the seam is clean
    L = n_(loop)
    t2 = np.arange(2 * L) / SR
    cyc = lambda hz: round(hz * loop) / loop
    dr = (blep_saw(cyc(midi(26)) * (1 + 0.002 * np.sin(2 * np.pi * cyc(0.13) * t2))) * 0.6
          + blep_saw(cyc(midi(33)) * (1 + 0.002 * np.sin(2 * np.pi * cyc(0.11) * t2 + 1))) * 0.45)
    dr = lp(dr, 380, 2)[L:]
    drone_t = t2[:L]
    level = np.ones(L) * 0.9
    b_start, b_end = 20 * bar, 28 * bar
    level *= np.clip(np.abs(drone_t - (b_start + b_end) / 2) / ((b_end - b_start) / 2), 0.15, 1)
    level *= 0.75 + 0.25 * np.sin(2 * np.pi * drone_t / (4 * bar))
    mix.add(dr * level, 0, 0.0, 0.13, 0.5)

    pat_up = [0, 1, 2, 3, 4, 3]
    pat_down = [0, 2, 4, 5, 4, 2]
    for b, name in enumerate(bars):
        root, voicing = T_CH[name]
        t0 = b * bar
        intro, outro = b < 4, b >= nb - 4
        pat = pat_up if b % 2 == 0 else pat_down
        vel = [1.0, 0.65, 0.7, 0.75, 0.7, 0.6]
        arp_harp(mix, t0, voicing, pat, e8, pan=-0.3, gain=0.24 if not (intro or outro) else 0.3, rev=0.4, var=b,
                 vel=vel)
        # the bass string on each downbeat, an octave down
        mix.add(harp(root + 12, 3.2) * 0.9, t0, -0.1, 0.22, 0.4)
        # strings through B and C
        if 20 <= b < nb - 4:
            ch = voicing[1:4]
            for side, seed in ((-0.65, b), (0.65, b + 100)):
                mix.add(pad(ch, bar + 0.1, "strings", seed=seed, a=0.5 if b == 20 else 0.25, r=0.9, cutoff=1700),
                        t0, side, 0.10, 0.5)
        # the choir in C (and the last half of B, softly)
        if 24 <= b < nb - 4:
            ch = [voicing[2], voicing[3], voicing[4]]
            soft = b < 28
            for side, seed in ((-0.5, b + 7), (0.5, b + 77)):
                mix.add(pad(ch, bar + 0.1, "choir", seed=seed, a=0.7 if b in (24, 28) else 0.3, r=1.2,
                            vowel="oo" if soft else "ah"), t0, side, 0.07 if soft else 0.11, 0.65)
        # frame drum: from the lift to the top
        if 22 <= b < nb - 4:
            heavy = b >= 28
            mix.add(frame_drum(1.0, True, b), t0, 0.1, 0.35 if heavy else 0.22, 0.3)
            mix.add(frame_drum(0.6, True, b + 1), t0 + 3 * e8, 0.1, 0.25 if heavy else 0.16, 0.3)
            for k in (4, 5) if heavy else (5,):
                mix.add(frame_drum(0.5, False, b * 7 + k), t0 + k * e8, 0.2, 0.18, 0.25)
            if b in (27, 35):       # a roll into the next section
                for k in range(6):
                    mix.add(frame_drum(0.3 + 0.1 * k, False, k), t0 + 3 * e8 + k * e8 / 2, 0.2, 0.2, 0.25)

    # the tune: flute for A and A', the strings (a viol section) for B, all together for C
    fl = (line(4, T_MEL_A, e8, 6, 0.8) + line(12, T_MEL_A2, e8, 6, 0.85)
          + line(nb - 4, T_MEL_OUT, e8, 6, 0.55))
    mix.add(legato(fl, total, seed=11, **FLUTE), 0, 0.25, 0.30, 0.45)
    vb = line(20, T_MEL_B, e8, 6, 0.85) + line(28, T_MEL_C, e8, 6, 1.0)
    vi = viol([(s, d, m - 12, v) for s, d, m, v in vb], total, seed=5)
    vi2 = viol(vb, total, seed=6)
    mix.add(vi, 0, -0.15, 0.20, 0.5)
    mix.add(vi2, 0, 0.2, 0.13, 0.5)
    fc = line(28, T_MEL_C, e8, 6, 0.75)
    mix.add(legato([(s, d, m + 12, v) for s, d, m, v in fc], total, seed=12, **FLUTE), 0, 0.35, 0.12, 0.5)
    # a far bell at the top of each big section
    for b in (4, 20, 28):
        mix.add(bell(50, 7.0), b * bar, -0.4, 0.07, 0.8)

    st = mix.render(hall_ir(2.6, 3.4, seed=3))
    st = shape(st, [(0, 0.5), (4 * bar, 0.55), (4.5 * bar, 0.68), (12 * bar, 0.72), (20 * bar, 0.8), (27 * bar, 0.95),
                    (28 * bar, 1.05), (35 * bar, 1.05), (36 * bar, 0.7), (nb * bar, 0.5), (total, 0.5)])
    return master(st, loop_n=n_(loop))


# ------------------------------------------------------------ 2. hall of fame: "The Hall of the Fallen"
# A minor, a slow sarabande in 3/4 (the weight on the second beat). A viol sings the names,
# a flute answers an octave up, and the choir hums under the last strain.

F_CH = {
    "Am": (45, [57, 60, 64, 69]), "F": (41, [53, 57, 60, 65]), "C": (48, [55, 60, 64, 67]),
    "G": (43, [55, 59, 62, 67]), "Dm": (50, [57, 62, 65, 69]), "E": (40, [56, 59, 64, 68]),
    "Em": (40, [55, 59, 64, 67]),
}
F_FORM = (["Am", "F", "C", "G", "F", "Dm", "E", "E"],
          ["Am", "Em", "F", "C", "Dm", "Am", "Dm", "E"],
          ["F", "G", "Em", "Am", "Dm", "G", "E", "Am"])
F_MEL_A = [(57, 2), (60, 1), (65, 2), (64, 1), (64, 1), (62, 1), (60, 1), (62, 3),
           (60, 2), (57, 1), (62, 2), (65, 1), (64, 2), (62, 1), (59, 3)]
F_MEL_B = [(69, 2), (72, 1), (71, 2), (67, 1), (69, 1), (72, 1), (77, 1), (76, 3),
           (74, 2), (72, 1), (72, 2), (69, 1), (65, 2), (69, 1), (68, 3)]
F_MEL_C = [(72, 2), (77, 1), (79, 2), (74, 1), (76, 2), (71, 1), (72, 3),
           (74, 2), (77, 1), (74, 1), (71, 1), (67, 1), (68, 2), (71, 1), (69, 3)]


def music_fame():
    q = 60.0 / 63            # one quarter note
    bar = 3 * q
    bars = [c for part in F_FORM for c in part]
    nb = len(bars)
    loop = nb * bar
    tail = 5.5
    total = loop + tail
    mix = Mix(total)

    for b, name in enumerate(bars):
        root, v = F_CH[name]
        t0 = b * bar
        # harp: a rolled chord on one, a soft broken figure on two and three
        for k, m in enumerate(v):
            mix.add(harp(m, 3.4, var=k % 2) * (0.85 - 0.08 * k), t0 + k * 0.035, -0.35 + 0.12 * k, 0.2, 0.45)
        mix.add(harp(root, 3.6) * 0.9, t0, -0.15, 0.22, 0.4)
        for k, idx in enumerate((2, 1, 3, 1)):
            mix.add(harp(v[idx] + 12, 2.0, var=k % 2) * 0.45, t0 + q + k * q / 2, 0.3, 0.16, 0.45)
        # a quiet bed of strings, leaning on the second beat like a sarabande
        for side, seed in ((-0.6, b), (0.6, b + 50)):
            s = pad(v[:3], bar + 0.15, "strings", seed=seed, a=0.35, r=1.0, cutoff=1300)
            k = np.arange(len(s)) / SR
            s *= 0.75 + 0.35 * np.exp(-((k - q * 1.2) / (q * 0.6)) ** 2)
            mix.add(s, t0, side, 0.075, 0.55)
        # the choir hums the last strain
        if b >= 16:
            for side, seed in ((-0.45, b + 3), (0.45, b + 33)):
                mix.add(pad(v[1:4], bar + 0.15, "choir", seed=seed, a=0.6 if b == 16 else 0.3, r=1.4,
                            vowel="oo" if b < 20 else "oh"), t0, side, 0.10, 0.7)
        # a low bell at each strain, like a name read out in a stone hall
        if b % 8 == 0:
            mix.add(bell(45, 8.0), t0, 0.35, 0.09, 0.85)
        if b == nb - 1:
            mix.add(timpani(45, 0.45, 3.0), t0, 0.0, 0.25, 0.5)

    vi = line(0, F_MEL_A, q, 3, 0.85) + line(16, F_MEL_C, q, 3, 0.95)
    vi = [(s, d * 0.98, m, vv) for s, d, m, vv in vi]
    mix.add(viol(vi, total, seed=21), 0, -0.1, 0.32, 0.5)
    fl = line(8, F_MEL_B, q, 3, 0.75) + line(16, [(m + 12 if m else None, ln) for m, ln in F_MEL_C], q, 3, 0.45)
    mix.add(legato(fl, total, seed=22, **FLUTE), 0, 0.3, 0.26, 0.5)
    # under the flute, the viol keeps a slow counterline on the chord roots
    cl = [(b * bar, bar * 0.98, F_CH[bars[b]][0] + 12, 0.6) for b in range(8, 16)]
    mix.add(viol(cl, total, seed=23), 0, -0.25, 0.17, 0.5)

    st = mix.render(hall_ir(3.2, 4.0, seed=5, dark=0.8))
    st = shape(st, [(0, 0.75), (8 * bar, 0.8), (16 * bar, 1.0), (22 * bar, 1.0), (nb * bar, 0.75), (total, 0.75)])
    return master(st, loop_n=n_(loop), target=0.15)


# ------------------------------------------------------------ 3. death: "Lament"
# D minor, slow. A great bell tolls; a viol falls away over a cold choir; the bell again;
# the last open fifth is left hanging, and the wind takes it.

def music_death():
    q = 60.0 / 54
    bar = 4 * q
    chords = ["Dm", "Bb", "Gm", "A", "Dm", "Gm", "A", "Dm", "Dm"]
    vo = {"Dm": [50, 57, 62, 65], "Bb": [46, 53, 58, 62], "Gm": [43, 50, 55, 58], "A": [45, 52, 57, 61]}
    total = len(chords) * bar + 7.0
    mix = Mix(total)
    mel = [(62, 2), (65, 1), (64, 1), (62, 3), (60, 1), (58, 2), (62, 2), (61, 4),
           (65, 2), (69, 2), (67, 2), (65, 1), (64, 1), (64, 2), (61, 2), (62, 8)]
    notes = line(0.5, mel, q, 4, 0.8)
    notes = [(s, d, m, 0.75 + 0.2 * (i in (8, 9))) for i, (s, d, m, _) in enumerate(notes)]
    mix.add(viol(notes, total, seed=31), 0, 0.0, 0.34, 0.55)
    for b, name in enumerate(chords):
        t0 = b * bar
        last = b == len(chords) - 1
        v = vo[name]
        ch = [50, 57] if last else v[1:]     # it ends on a bare fifth
        for side, seed in ((-0.55, b), (0.55, b + 9)):
            mix.add(pad(ch, bar + (3.0 if last else 0.1), "choir", seed=seed, a=0.9 if b == 0 else 0.5,
                        r=3.0 if last else 1.2, vowel="oo"), t0, side, 0.09, 0.75)
            mix.add(pad([v[0]], bar + 0.1, "strings", seed=seed + 40, a=0.6, r=1.5, cutoff=700), t0, side * 0.5,
                    0.11, 0.6)
    for b in (0, 4, 8):
        mix.add(bell(38, 9.0), b * bar, -0.2, 0.22, 0.75)
        mix.add(bell(50, 6.0), b * bar + 0.01, 0.2, 0.05, 0.75)
    mix.add(timpani(38, 0.35, 4.0), 7 * bar, 0.0, 0.3, 0.5)
    w = wind(total, 4)
    k = np.arange(len(w)) / SR
    mix.add(w * np.clip((k - 6 * bar) / (2 * bar), 0, 1) * np.clip((total - k) / 3.0, 0, 1), 0, 0.3, 0.05, 0.2)
    st = mix.render(hall_ir(3.4, 4.5, seed=9, dark=0.7))
    fade = n_(2.5)
    st[:, -fade:] *= np.linspace(1, 0, fade) ** 2
    return master(st, target=0.14)


# ------------------------------------------------------------ 4. victory: "The Heartflame"
# D major. A timpani roll and a choir swell as the fire catches; a fanfare; bells peal; then
# the title's tune comes back as a hymn in the major, and everything rings out together.

def music_victory():
    q = 60.0 / 88
    e8 = q / 2
    bar = 4 * q
    V = {"D": (38, [50, 57, 62, 66]), "A": (33, [45, 52, 57, 61]), "G": (43, [50, 55, 59, 62]),
         "Bm": (35, [47, 54, 59, 62]), "Em": (40, [52, 55, 59, 64]), "A7": (33, [45, 52, 55, 61]),
         "F#m": (42, [49, 54, 57, 61])}
    intro = 2
    fanfare = 4
    hymn = ["D", "A", "G", "D", "Bm", "G", "A7", "D", "G", "D", "Em", "A7", "D", "G", "A7", "D"]
    hymn0 = intro + fanfare
    nbar = hymn0 + len(hymn) + 2
    total = nbar * bar + 7.0
    mix = Mix(total)

    # bars 0-1: the fire catches. A swelling roll, the choir rising out of nothing
    timp_roll(45, 0.0, 2 * bar - 0.1, 0.12, 1.0, mix, 0.0, 0.5, 0.35, seed=1)
    for side, seed in ((-0.5, 1), (0.5, 2)):
        c = pad([50, 57, 62, 66, 69], 2 * bar + 0.3, "choir", seed=seed, a=2 * bar * 0.9, r=0.4, vowel="ah")
        mix.add(c, 0, side, 0.16, 0.7)
    sh = hp(np.random.default_rng(3).normal(size=n_(2 * bar)), 5000)
    sh *= np.linspace(0, 1, len(sh)) ** 3
    mix.add(sh, 0, 0.0, 0.05, 0.6)
    mix.add(crash(3.0, 1), 2 * bar, 0.1, 0.16, 0.6)

    # bars 2-5: the fanfare, two trumpets and the timpani on the accents
    t_f = intro * bar
    fan = [(62, 1), (66, 1), (69, 2), (74, 3), (None, 1),
           (74, 1), (76, 1), (78, 2), (81, 4),
           (79, 2), (78, 1), (76, 1), (74, 2), (71, 2),
           (73, 2), (76, 2), (81, 4)]
    fan2 = [(None, 4), (66, 3), (None, 1),
            (69, 1), (69, 1), (74, 2), (78, 4),
            (74, 2), (74, 1), (71, 1), (71, 2), (67, 2),
            (69, 2), (73, 2), (76, 4)]
    for mel, pan, g in ((fan, 0.25, 0.30), (fan2, -0.25, 0.24)):
        s = 0
        for m, ln in mel:
            if m is not None:
                mix.add(brass(m, ln * e8 * 0.92, 0.9 if ln < 3 else 1.0, seed=s), t_f + s * e8, pan, g, 0.4)
            s += ln
    for b, (name, acc) in enumerate((("D", (0, 4, 6)), ("D", (0, 4)), ("G", (0, 4)), ("A", (0, 4)))):
        root, v = V[name]
        t0 = t_f + b * bar
        for k in acc:
            mix.add(timpani(45 if name == "A" else 50 if name != "G" else 43, 0.9, 2.5), t0 + k * e8, 0.0, 0.42, 0.35)
        for side, seed in ((-0.6, b), (0.6, b + 5)):
            mix.add(pad(v[1:], bar + 0.1, "strings", seed=seed, a=0.08, r=0.6, cutoff=2600), t0, side, 0.12, 0.45)
        mix.add(brass(root + 12, bar * 0.95, 0.7, seed=b), t0, -0.1, 0.16, 0.4)

    # the bells: rounds on eight bells, down the scale, through the fanfare and into the hymn
    rounds = [86, 85, 83, 81, 79, 78, 76, 74]
    rng = np.random.default_rng(8)
    s = t_f + 2 * bar
    k = 0
    while s < (hymn0 + 2) * bar:
        m = rounds[k % 8]
        fade = np.clip(((hymn0 + 2) * bar - s) / (2 * bar), 0, 1)
        mix.add(bell(m - 12, 3.5, "hand"), s, -0.7 + 1.4 * (k % 8) / 7, 0.09 * (0.5 + 0.5 * fade) * rng.uniform(0.8, 1),
                0.6)
        s += e8 * (1.0 if k % 8 != 7 else 2.0)
        k += 1

    # the hymn: the title tune, brought up into the light
    h1 = [(69, 2), (74, 4), (78, 2), (76, 4), (73, 2), (69, 2), (71, 4), (74, 2), (79, 2), (78, 6), (76, 2),
          (74, 4), (78, 2), (83, 2), (83, 4), (81, 2), (79, 2), (76, 4), (79, 2), (73, 2), (74, 8)]
    h2 = [(79, 4), (83, 2), (86, 2), (86, 4), (81, 2), (78, 2), (79, 4), (76, 2), (83, 2), (81, 6), (79, 2),
          (78, 4), (81, 2), (86, 2), (83, 4), (79, 2), (83, 2), (81, 4), (79, 2), (76, 2), (74, 8)]
    t_h = hymn0 * bar
    fl = line(hymn0, h1, e8, 8, 0.8)
    mix.add(legato(fl, total, seed=41, **FLUTE), 0, 0.25, 0.30, 0.45)
    v1 = viol([(s, d, m - 12, v) for s, d, m, v in fl], total, seed=42)
    mix.add(v1, 0, -0.2, 0.14, 0.5)
    sec = line(hymn0 + 8, h2, e8, 8, 1.0)
    mix.add(viol(sec, total, seed=43), 0, -0.15, 0.26, 0.5)
    mix.add(viol([(s, d, m - 12, v) for s, d, m, v in sec], total, seed=44), 0, 0.15, 0.2, 0.5)
    hn = legato([(s, d, m - 12, v * 0.9) for s, d, m, v in sec], total, seed=45, **HORN)
    mix.add(lp(hn, 3000), 0, 0.3, 0.18, 0.45)
    for b, name in enumerate(hymn):
        root, v = V[name]
        t0 = t_h + b * bar
        big = b >= 8
        for k in range(8):
            idx = (0, 1, 2, 3, 2, 1, 2, 3)[k]
            mix.add(harp(v[idx] + 12, 2.5, var=k % 2) * (1.0 if k % 4 == 0 else 0.7), t0 + k * e8, -0.35,
                    0.17 if big else 0.22, 0.4)
        mix.add(harp(root + 12, 3.0) * 0.9, t0, -0.1, 0.2, 0.4)
        for side, seed in ((-0.6, b + 10), (0.6, b + 60)):
            mix.add(pad(v[1:], bar + 0.1, "strings", seed=seed, a=0.3 if b in (0, 8) else 0.12, r=0.8,
                        cutoff=2200 if big else 1600), t0, side, 0.10 if big else 0.08, 0.5)
            if big:
                mix.add(pad(v[1:], bar + 0.1, "choir", seed=seed + 5, a=0.4 if b == 8 else 0.15, r=1.0, vowel="ah"),
                        t0, side * 0.8, 0.12, 0.65)
        if big:
            mix.add(timpani(50 if name in ("D", "G", "Bm") else 45, 0.7, 2.5), t0, 0.0, 0.3, 0.35)
            if b % 2 == 1:
                mix.add(timpani(45 if name in ("D", "G", "Bm") else 50, 0.45, 2.0), t0 + 2 * q, 0.0, 0.25, 0.35)
    # the last chord: everything, with a roll and the bells pealing again
    t_end = (hymn0 + len(hymn)) * bar
    for m in (50, 57, 62, 66, 69, 74):
        mix.add(brass(m, 2 * bar, 0.85, seed=m), t_end, -0.4 + 0.15 * (m % 7), 0.12, 0.45)
    for side, seed in ((-0.6, 91), (0.6, 92)):
        mix.add(pad([62, 66, 69, 74], 2 * bar, "choir", seed=seed, a=0.2, r=3.0, vowel="ah"), t_end, side, 0.14, 0.7)
        mix.add(pad([50, 57, 62, 66], 2 * bar, "strings", seed=seed + 1, a=0.1, r=3.0, cutoff=2400), t_end, side,
                0.12, 0.5)
    timp_roll(50, t_end, 2 * bar - 0.4, 0.5, 1.0, mix, 0.0, 0.45, 0.35, seed=9)
    mix.add(timpani(38, 1.0, 4.0), t_end + 2 * bar - 0.3, 0.0, 0.5, 0.4)
    mix.add(bell(50, 9.0), t_end, -0.3, 0.14, 0.8)
    mix.add(crash(4.0, 2), t_end + 2 * bar - 0.3, 0.0, 0.14, 0.6)
    mix.add(bell(62, 8.0), t_end + 0.02, 0.3, 0.08, 0.8)
    s = t_end + 0.5
    for k in range(16):
        m = rounds[k % 8]
        mix.add(bell(m - 12, 3.5, "hand") * (1 - k / 18), s, -0.7 + 1.4 * (k % 8) / 7, 0.08, 0.65)
        s += e8
    st = mix.render(hall_ir(3.0, 4.2, seed=11))
    st = shape(st, [(0, 1.0), (hymn0 * bar, 1.0), (hymn0 * bar + 0.5, 0.72), ((hymn0 + 7) * bar, 0.78),
                    ((hymn0 + 8) * bar, 1.0), (t_end, 1.05), (total, 1.1)])
    fade = n_(2.0)
    st[:, -fade:] *= np.linspace(1, 0, fade) ** 2
    return master(st, target=0.17)


# ------------------------------------------------------------ main

PIECES = {   # name -> (function, file base)
    "title": (music_title, "music_title"),
    "fame": (music_fame, "music_fame"),
    "death": (music_death, "music_death"),
    "victory": (music_victory, "music_victory"),
}


def write(path, st, ogg):
    pcm = (np.clip(st.T, -1, 1) * 32767).astype(np.int16)
    if not ogg:
        wavfile.write(path, SR, pcm)
        return
    with tempfile.TemporaryDirectory() as d:
        tmp = os.path.join(d, "m.wav")
        wavfile.write(tmp, SR, pcm)
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", tmp, "-c:a", "libvorbis", "-q:a", "6", path],
                       check=True)


def main(argv):
    args = argv[1:]
    if "-h" in args or "--help" in args:
        print(__doc__)
        return 0
    if "-V" in args or "--version" in args:
        print(f"make_music {VERSION}")
        return 0
    force = "--force" in args
    wav = "--wav" in args
    args = [a for a in args if a not in ("--force", "--wav")]
    only = None
    if "--only" in args:
        i = args.index("--only")
        only = set(args[i + 1:])
        args = args[:i]
        bad = only - set(PIECES)
        if bad:
            print(f"make_music: no piece called {', '.join(sorted(bad))} (there's {', '.join(PIECES)})")
            return 2
    out = args[0] if args else "assets/sounds"
    ogg = not wav and shutil.which("ffmpeg") is not None
    if not wav and not ogg:
        print("make_music: no ffmpeg, so writing .wav files (the game reads those too)")
    os.makedirs(out, exist_ok=True)
    for name, (fn, base) in PIECES.items():
        if only and name not in only:
            continue
        path = os.path.join(out, base + (".ogg" if ogg else ".wav"))
        if os.path.exists(path) and not force and not only:
            print(f"  {path}  kept")
            continue
        st = fn()
        write(path, st, ogg)
        print(f"  {path}  {st.shape[1] / SR:.1f}s  {os.path.getsize(path) // 1024} KB")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
