#!/usr/bin/env python3
"""Synthesize Lume's sound effects and ambient loops as WAV files.

Everything is made from noise and oscillators, then put in a small stone room
(a convolution reverb), so no recorded samples are needed. Some events get
several variants (step_a/_b/_c...) and the game picks one at random.

assets/sounds/sounds.txt maps game events to files and volumes. It is only
written when missing, so hand edits survive re-runs.

Existing files are kept (you may have swapped in your own), except for sounds
named with --only, or everything with --force.

Run: python3 tools/make_sounds.py [out_dir]          (default assets/sounds)
     python3 tools/make_sounds.py --only NAME...     render just these sounds
     python3 tools/make_sounds.py --force            re-render and overwrite all
"""
import os
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, lfilter, fftconvolve

VERSION = "1.3.0"
SR = 44100
RNG = np.random.default_rng(2611)


# ------------------------------------------------------------ primitives

def n_(d):
    return max(1, int(SR * d))


def tt(d):
    return np.arange(n_(d)) / SR


def phase(freq, n):
    f = np.broadcast_to(np.asarray(freq, dtype=np.float64), (n,))
    return np.cumsum(f) / SR


def sine(freq, d):
    return np.sin(2 * np.pi * phase(freq, n_(d)))


def saw(freq, d):
    return 2 * (phase(freq, n_(d)) % 1.0) - 1


def noise(d):
    return RNG.uniform(-1, 1, n_(d))


def brown(d):
    x = np.cumsum(RNG.normal(0, 1, n_(d)))
    x = hp(x, 20)
    return x / (np.max(np.abs(x)) or 1)


def slide(f0, f1, d):
    x = tt(d) / d
    return f0 * (f1 / f0) ** x


def env(d, a=0.005, decay=None, sustain=1.0, release=0.05, power=1.0):
    n = n_(d)
    e = np.ones(n)
    na = min(n, n_(a))
    e[:na] = np.linspace(0, 1, na)
    if decay:
        nd = np.arange(n - na)
        e[na:] = sustain + (1 - sustain) * np.exp(-nd / (decay * SR))
    nr = min(n, n_(release))
    e[-nr:] *= np.linspace(1, 0, nr)
    return e ** power


def lp(x, hz, order=2):
    b, a = butter(order, min(hz / (SR / 2), 0.99))
    return lfilter(b, a, x)


def hp(x, hz, order=2):
    b, a = butter(order, max(hz / (SR / 2), 0.0005), "high")
    return lfilter(b, a, x)


def bp(x, lo, hi, order=2):
    return hp(lp(x, hi, order), lo, order)


def sweep_lp(x, f0, f1):
    """One-pole low-pass whose cutoff glides from f0 to f1 (exponentially)."""
    n = len(x)
    fc = f0 * (f1 / f0) ** (np.arange(n) / max(1, n - 1))
    a = np.exp(-2 * np.pi * fc / SR)
    y = np.empty(n)
    s = 0.0
    for i in range(n):
        s = a[i] * s + (1 - a[i]) * x[i]
        y[i] = s
    return y


def pad_to(x, n):
    return np.pad(x, (0, max(0, n - len(x))))[:n]


def mix(*parts):
    n = max(len(p) for p in parts)
    return sum(pad_to(p, n) for p in parts)


def at(x, t):
    """Delay x by t seconds."""
    return np.concatenate([np.zeros(n_(t)), x])


def crackles(d, rate, lo=1500, hi=7000, decay=0.004, seed_amp=1.0):
    """Sparse pops and snaps, like burning pitch."""
    n = n_(d)
    out = np.zeros(n)
    k = RNG.poisson(rate * d)
    for _ in range(k):
        i = RNG.integers(0, n)
        ln = n_(RNG.uniform(0.002, 0.012))
        pop = RNG.uniform(-1, 1, ln) * np.exp(-np.arange(ln) / (decay * SR)) * RNG.uniform(0.2, 1.0) ** 2
        out[i:i + ln] += pop[:n - i] * seed_amp
    return bp(out, lo, hi)


# a small, damp stone room
_IR = None


def room_ir():
    global _IR
    if _IR is None:
        d = 1.6
        t = tt(d)
        ir = RNG.normal(0, 1, len(t)) * np.exp(-t / 0.32)
        ir = lp(ir, 3800) * 0.6 + lp(ir, 900) * 0.4
        # a few early reflections off near walls
        for ms, g in ((7, 0.6), (13, 0.45), (19, 0.35), (31, 0.25)):
            ir[n_(ms / 1000)] += g * 6
        ir[:n_(0.004)] = 0
        _IR = ir / np.sqrt(np.sum(ir * ir))
    return _IR


def room(x, wet=0.22, tail=1.2):
    x = np.pad(np.asarray(x, dtype=np.float64), (0, n_(tail)))
    w = fftconvolve(x, room_ir())[:len(x)]
    w *= (np.max(np.abs(x)) or 1) / (np.max(np.abs(w)) or 1)
    return x * (1 - wet) + w * wet


def loopable(x, fade=1.0):
    """Fold the tail over the head so the clip loops without a seam."""
    f = n_(fade)
    body = x[:-f].copy()
    ramp = np.linspace(0, 1, f)
    body[:f] = body[:f] * ramp + x[-f:] * (1 - ramp)
    return body


def write(path, x, peak=0.89):
    x = np.asarray(x, dtype=np.float64)
    m = np.max(np.abs(x)) or 1.0
    x = x / m * peak
    # trim the reverb tail once it falls below -54 dB
    quiet = np.abs(x) > peak * 0.002
    if quiet.any():
        end = min(len(x), np.nonzero(quiet)[0][-1] + n_(0.02))
        x = x[:end]
    f = min(len(x) // 4, n_(0.003))
    if f:
        x[:f] *= np.linspace(0, 1, f)
        x[-f:] *= np.linspace(1, 0, f)
    wavfile.write(path, SR, (x * 32767).astype(np.int16))


def write_loop(path, x, peak=0.89):
    """Like write(), but without the fade at the ends (it loops)."""
    x = np.asarray(x, dtype=np.float64)
    m = np.max(np.abs(x)) or 1.0
    wavfile.write(path, SR, (x / m * peak * 32767).astype(np.int16))


# ---------------------------------------------------------- footsteps & body

def step(v):
    """A boot on flagstone: a dull knock and a little grit."""
    d = 0.16
    knock = lp(noise(d), 600 + v * 120) * env(d, 0.002, 0.018, 0)
    body = sine(slide(150 + v * 12, 90, d), d) * env(d, 0.001, 0.025, 0) * 0.5
    grit = bp(noise(d), 2500, 7000) * env(d, 0.001, 0.012, 0) * 0.25
    scuff = at(bp(noise(0.06), 1500, 5000) * env(0.06, 0.02, 0.02, 0) * 0.12, 0.03)
    return room(mix(knock, body, grit, scuff), 0.18, 0.5)


def wade(v):
    """Ankle-deep water: a slap and a slosh."""
    d = 0.35
    slap = bp(noise(d), 500, 4000) * env(d, 0.003, 0.04, 0)
    slosh = sweep_lp(noise(d), 2500 + v * 300, 500) * env(d, 0.03, 0.12, 0) * 0.6
    drops = np.zeros(n_(d))
    for k in range(5):
        t0 = RNG.uniform(0.04, 0.22)
        f = RNG.uniform(900, 2200)
        dd = 0.05
        drops += pad_to(at(sine(slide(f, f * 1.6, dd), dd) * env(dd, 0.001, 0.012, 0) * 0.25, t0), n_(d))
    return room(mix(slap, slosh, drops), 0.22, 0.6)


def swim(v):
    d = 0.7
    surge = lp(sweep_lp(noise(d), 300, 1800 + v * 200), 1600, 4) * env(d, 0.15, 0.25, 0) * 2.5
    gurgle = mix(*[at(sine(slide(RNG.uniform(250, 500), RNG.uniform(500, 900), 0.06), 0.06) * env(0.06, 0.002, 0.02, 0) * 0.3,
                    RNG.uniform(0.1, 0.5)) for _ in range(6)])
    return room(mix(surge, gurgle), 0.25, 0.8)


def splash(v):
    """Something heavy hits the water."""
    d = 0.9
    hit = bp(noise(d), 300, 6000) * env(d, 0.002, 0.06, 0)
    plume = sweep_lp(noise(d), 5000, 400) * env(d, 0.02, 0.25, 0) * 0.7
    plunk = sine(slide(220 + v * 30, 90, 0.2), 0.2) * env(0.2, 0.002, 0.05, 0) * 0.6
    drops = mix(*[at(sine(slide(f, f * 1.5, 0.05), 0.05) * env(0.05, 0.001, 0.012, 0) * 0.3, RNG.uniform(0.1, 0.6))
                for f in RNG.uniform(800, 2600, 10)])
    return room(mix(hit, plume, plunk, drops), 0.3, 1.0)


def swing(v):
    """A short sword cutting the air."""
    d = 0.22
    x = noise(d)
    f0, f1 = (700, 2600) if v % 2 == 0 else (900, 3200)
    w = bp(sweep_lp(x, f0, f1), 400, 6000) * np.sin(np.pi * np.clip(tt(d) / d, 0, 1)) ** 2
    return room(w, 0.15, 0.4)


def hit(v):
    """Steel into flesh: a heavy thud with a wet crunch."""
    d = 0.3
    thud = sine(slide(120 + v * 15, 50, d), d) * env(d, 0.001, 0.06, 0)
    crunch = bp(noise(d), 600, 3500) * env(d, 0.001, 0.035, 0) * 0.7
    ring = sine(2350 + v * 90, d) * env(d, 0.001, 0.08, 0) * 0.05
    return room(mix(thud, crunch, ring), 0.2, 0.6)


def hurt(v):
    """You are struck: a deep knock and a short grunt."""
    d = 0.45
    knock = sine(slide(90, 40, d), d) * env(d, 0.001, 0.09, 0)
    grunt_f = slide(150 - v * 10, 105, 0.22)
    grunt = bp(saw(grunt_f, 0.22), 300, 1400) * env(0.22, 0.01, 0.08, 0) * 0.45
    hiss = bp(noise(0.2), 1200, 3500) * env(0.2, 0.005, 0.05, 0) * 0.15
    return room(mix(knock, at(grunt, 0.02), hiss), 0.22, 0.8)


def die(v):
    """Death: a long falling drone and a bell-like toll."""
    d = 3.0
    drone = (saw(slide(110, 55, d), d) + saw(slide(110.6, 55.4, d), d)) * 0.5
    drone = lp(drone, 700) * env(d, 0.05, 1.2, 0)
    toll = sum(sine(f, d) * env(d, 0.002, dd, 0) * a
               for f, dd, a in ((196, 1.6, 0.5), (392 * 1.002, 0.9, 0.25), (587, 0.5, 0.12), (523.3 * 1.5, 0.3, 0.06)))
    thump = sine(slide(70, 35, 0.6), 0.6) * env(0.6, 0.001, 0.15, 0)
    return room(mix(drone * 0.6, toll, thump), 0.4, 2.0)


def kill(v):
    """A creature dies: a crunch and a falling body."""
    d = 0.6
    crunch = bp(noise(0.12), 400, 3000) * env(0.12, 0.001, 0.03, 0)
    fall = at(lp(noise(0.25), 500) * env(0.25, 0.005, 0.06, 0) * 0.8, 0.12)
    thud = at(sine(slide(80, 45, 0.25), 0.25) * env(0.25, 0.001, 0.07, 0) * 0.8, 0.12)
    return room(mix(crunch, fall, thud), 0.25, 0.8)


# ---------------------------------------------------------- creatures

def rat(v):
    """A squeak or two."""
    out = np.zeros(1)
    n = 2 if v != 1 else 3
    for k in range(n):
        d = RNG.uniform(0.06, 0.11)
        f0 = RNG.uniform(2600, 3600)
        f = slide(f0, f0 * RNG.uniform(1.15, 1.4), d) * (1 + 0.04 * np.sin(2 * np.pi * 38 * tt(d)))
        sq = (sine(f, d) + 0.3 * sine(f * 2, d)) * env(d, 0.006, None, 1, 0.03)
        out = mix(out, at(sq, k * RNG.uniform(0.09, 0.14)))
    return room(out, 0.25, 0.5)


def goblin(v):
    """A wet, nasal snarl."""
    d = 0.55 + v * 0.05
    f = slide(170 + v * 15, 120, d) * (1 + 0.06 * np.sin(2 * np.pi * (11 + v * 2) * tt(d)))
    raw = saw(f, d) + 0.3 * noise(d)
    voice = bp(raw, 500, 900, 2) * 1.2 + bp(raw, 1600, 2400, 2) * 0.6 + bp(raw, 150, 350) * 0.6
    voice *= env(d, 0.03, None, 1, 0.12) * (0.7 + 0.3 * np.abs(np.sin(2 * np.pi * 7 * tt(d))))
    return room(voice, 0.25, 0.8)


def bat(v):
    """Leathery wingbeats and a thin chitter."""
    out = np.zeros(1)
    for k in range(5 + v):
        d = 0.05
        flap = bp(noise(d), 300, 1400) * env(d, 0.004, 0.012, 0) * RNG.uniform(0.4, 0.8)
        out = mix(out, at(flap, k * 0.075 + RNG.uniform(0, 0.01)))
    for k in range(2 + v % 2):
        d = RNG.uniform(0.025, 0.045)
        f0 = RNG.uniform(5200, 7000)
        ch = sine(slide(f0, f0 * 0.8, d), d) * env(d, 0.002, None, 1, 0.01) * 0.35
        out = mix(out, at(ch, 0.05 + k * RNG.uniform(0.06, 0.1)))
    return room(out, 0.2, 0.4)


def ogre(v):
    """A deep, wet growl from a big chest."""
    d = 0.9 + v * 0.1
    t = tt(d)
    f = slide(78 + v * 6, 58, d) * (1 + 0.08 * np.sin(2 * np.pi * (6 + v) * t))
    raw = saw(f, d) + 0.5 * brown(d)
    voice = lp(raw, 520, 2) * 1.3 + bp(raw, 280, 520) * 0.7 + bp(raw, 900, 1300) * 0.15
    voice *= env(d, 0.06, None, 1, 0.25) * (0.75 + 0.25 * np.abs(np.sin(2 * np.pi * 4.5 * t)))
    return room(voice, 0.3, 1.0)


def eel(v):
    """Something lunges up out of the water and snaps."""
    d = 0.6
    surge = sweep_lp(noise(d), 600, 3500) * env(d, 0.01, 0.12, 0) * 0.8
    snap = at(bp(noise(0.03), 1500, 6000) * env(0.03, 0.0005, 0.008, 0) * 1.2, 0.09 + v * 0.02)
    plunk = at(sine(slide(160, 80, 0.15), 0.15) * env(0.15, 0.002, 0.04, 0) * 0.5, 0.16)
    return room(mix(surge, snap, plunk), 0.3, 0.8)


def sling(v):
    """A sling whirled once, then loosed."""
    d = 0.42
    t = tt(d)
    whirr = bp(noise(d), 500, 2400) * (0.5 + 0.5 * np.sin(2 * np.pi * (14 + v * 3) * t) ** 2) * np.sin(np.pi * t / d) ** 2 * 0.6
    loose = at(bp(sweep_lp(noise(0.12), 4000, 900), 400, 5000) * env(0.12, 0.002, 0.04, 0), 0.3)
    return room(mix(whirr, loose), 0.2, 0.5)


def shade(v):
    """A breathy whisper that doesn't quite form words."""
    d = 1.3
    x = noise(d)
    t = tt(d)
    out = np.zeros(n_(d))
    # drifting formants make it sound like a voice without being one
    for f0, f1, a in ((500, 900, 1.0), (1300, 900, 0.7), (2500, 2900, 0.4)):
        seg = 4
        L = len(x) // seg
        for s in range(seg):
            fc = f0 + (f1 - f0) * (s + RNG.uniform(-0.3, 0.3)) / seg
            part = bp(x[s * L:(s + 1) * L + 200], fc * 0.85, fc * 1.15)
            out[s * L:s * L + len(part)] += part[:len(out) - s * L] * a
    out *= np.sin(np.pi * t / d) ** 1.5 * (0.6 + 0.4 * np.sin(2 * np.pi * (2.3 + v * 0.4) * t) ** 2)
    sub = sine(slide(70, 55, d), d) * env(d, 0.3, None, 1, 0.5) * 0.15
    return room(mix(out, sub), 0.5, 1.6)


def burn(v):
    """Firelight on a shade: a searing sizzle."""
    d = 0.7
    sizzle = hp(noise(d), 3000) * env(d, 0.01, 0.25, 0) * (0.7 + 0.3 * np.sin(2 * np.pi * 31 * tt(d)))
    pops = crackles(d, 60, 2000, 8000) * 1.5
    moan = bp(saw(slide(240, 160, d), d), 400, 1200) * env(d, 0.05, 0.3, 0) * 0.25
    return room(lp(mix(sizzle, pops, moan), 6500, 2), 0.3, 0.9)


def dissolve(v):
    """A shade comes apart: a long exhale and a cold shimmer."""
    d = 1.6
    t = tt(d)
    breath = sweep_lp(noise(d), 3000, 300) * np.exp(-t / 0.5) * np.clip(t / 0.05, 0, 1)
    shimmer = sum(sine(f * (1 + 0.003 * np.sin(2 * np.pi * 5 * t)), d) * np.exp(-t / 0.7) * 0.08
                  for f in (880, 1320, 1975, 2637))
    return room(mix(breath, shimmer), 0.5, 1.8)


def alert(v):
    """Something has come into view: a low, uneasy swell."""
    d = 1.2
    t = tt(d)
    e = np.sin(np.pi * np.clip(t / d, 0, 1)) ** 2
    tone = (sine(73.4, d) + sine(77.8, d) * 0.8 + sine(146.8 * 1.003, d) * 0.3) * e
    air = bp(noise(d), 200, 600) * e * 0.25
    return room(mix(tone, air), 0.4, 1.4)


# ---------------------------------------------------------- fire & light

def ignite(v):
    """A torch catching: a soft whump and the first crackles."""
    d = 0.8
    whump = sweep_lp(noise(d), 200, 2500) * env(d, 0.04, 0.18, 0)
    body = sine(slide(70, 110, 0.3), 0.3) * env(0.3, 0.02, 0.08, 0) * 0.5
    pops = at(crackles(0.6, 40), 0.1) * 0.6
    return room(mix(whump, body, pops), 0.2, 0.8)


def sconce(v):
    """A sconce flaring up: a bigger whoosh."""
    d = 1.1
    whoosh = sweep_lp(noise(d), 150, 3500) * env(d, 0.08, 0.3, 0)
    body = sine(slide(55, 95, 0.5), 0.5) * env(0.5, 0.05, 0.15, 0) * 0.5
    pops = at(crackles(0.9, 50), 0.15) * 0.7
    return room(mix(whoosh, body, pops), 0.25, 1.0)


def snuff(v):
    """Pinched out: a breathy puff and a dying hiss."""
    d = 0.5
    puff = lp(noise(d), 1200) * env(d, 0.005, 0.05, 0)
    hiss = hp(noise(d), 2500) * env(d, 0.01, 0.15, 0) * 0.35
    return room(lp(mix(puff, hiss), 5000, 2), 0.2, 0.6)


def hiss(v):
    """Fire meets water: steam."""
    d = 1.0
    t = tt(d)
    s = hp(noise(d), 1800) * np.exp(-t / 0.35) * np.clip(t / 0.01, 0, 1)
    s *= 0.75 + 0.25 * np.sin(2 * np.pi * 23 * t)
    bub = mix(*[at(sine(slide(f, f * 1.4, 0.04), 0.04) * env(0.04, 0.001, 0.01, 0) * 0.2, RNG.uniform(0.02, 0.5))
              for f in RNG.uniform(1000, 2500, 8)])
    return room(lp(mix(s, bub), 6000, 2), 0.25, 0.8)


# ---------------------------------------------------------- items

def throw(v):
    d = 0.3
    w = bp(sweep_lp(noise(d), 500, 2200), 300, 4000) * np.sin(np.pi * tt(d) / d) ** 3
    return room(w, 0.15, 0.4)


def clink(v):
    """Iron on stone: a bright tick and a skitter."""
    d = 0.5
    f = 3100 + v * 400
    tick = (sine(f, d) * 0.6 + sine(f * 1.47, d) * 0.3 + sine(f * 2.09, d) * 0.15) * env(d, 0.0005, 0.03, 0)
    click = hp(noise(0.01), 3000) * 0.6
    skitter = at((sine(f * 0.93, 0.2) * 0.3) * env(0.2, 0.0005, 0.02, 0), 0.07 + v * 0.01)
    return room(mix(tick, click, skitter), 0.25, 0.6)


def thud(v):
    """Something solid lands on the floor."""
    d = 0.4
    body = sine(slide(110, 55, d), d) * env(d, 0.001, 0.05, 0)
    knock = lp(noise(d), 800) * env(d, 0.001, 0.02, 0) * 0.7
    return room(mix(body, knock), 0.22, 0.6)


def shatter(v):
    """A flask breaks: glass shards and a splash of liquid."""
    d = 0.9
    crack = hp(noise(0.03), 2000) * env(0.03, 0.0005, 0.008, 0)
    shards = np.zeros(n_(d))
    for f in RNG.uniform(2500, 9000, 26):
        t0 = RNG.exponential(0.06)
        dd = RNG.uniform(0.05, 0.25)
        shards += pad_to(at(sine(f, dd) * env(dd, 0.0005, dd / 4, 0) * RNG.uniform(0.1, 0.4), t0), n_(d))
    wet = at(sweep_lp(noise(0.3), 3000, 600) * env(0.3, 0.01, 0.08, 0) * 0.4, 0.02)
    return room(mix(crack, shards, wet), 0.28, 0.9)


def drink(v):
    """Three gulps, then a warm chime as wounds close."""
    out = np.zeros(1)
    for k in range(3):
        d = 0.11
        g = sine(slide(220, 140, d), d) * env(d, 0.005, 0.03, 0) + lp(noise(d), 600) * env(d, 0.005, 0.02, 0) * 0.3
        out = mix(out, at(g, 0.05 + k * 0.17))
    d = 1.2
    chime = sum(sine(f, d) * env(d, 0.01, 0.4, 0) * a for f, a in ((523.3, 0.25), (659.3, 0.2), (784, 0.15), (1046.5, 0.08)))
    return room(mix(out, at(chime, 0.55)), 0.35, 1.2)


def pickup(v):
    """Into the pack: a rustle and a faint clink."""
    d = 0.25
    rustle = bp(noise(d), 1500, 6000) * env(d, 0.02, 0.05, 0) * (0.6 + 0.4 * np.sin(2 * np.pi * 37 * tt(d)))
    tick = at(sine(2600 + v * 300, 0.12) * env(0.12, 0.0005, 0.02, 0) * 0.3, 0.08)
    return room(mix(rustle, tick), 0.15, 0.4)


def door(v):
    """Old hinges, then the door bumping open."""
    d = 0.7
    t = tt(d)
    f = (140 + v * 20) * (1 + 0.25 * t / d) * (1 + 0.08 * np.sin(2 * np.pi * 9 * t))
    stick = (np.sin(2 * np.pi * 47 * t) > 0.3).astype(float) * 0.5 + 0.5  # stick-slip judder
    creak = (bp(saw(f, d), 600, 1400) + 0.5 * bp(saw(f, d), 1800, 2800)) * stick
    creak *= env(d, 0.05, None, 1, 0.15) * 0.8
    bump = at(lp(noise(0.12), 400) * env(0.12, 0.001, 0.03, 0), d - 0.1)
    return room(mix(creak, bump), 0.3, 0.8)


def stairs(v):
    """Footsteps going down into the dark."""
    out = np.zeros(1)
    for k in range(5):
        s = step(k % 3)[:n_(0.2)] * (1 - k * 0.12)
        out = mix(out, at(s * 0.8, k * 0.24))
    swell = sweep_lp(noise(1.6), 300, 120) * env(1.6, 0.4, None, 1, 0.6) * 0.35
    return room(mix(out, swell), 0.4, 1.5)


def fall(v):
    """Wind rushing past as you drop."""
    d = 1.0
    t = tt(d)
    wind = bp(sweep_lp(noise(d), 400, 3000), 200, 5000) * np.clip(t / 0.6, 0, 1) ** 2
    return room(wind, 0.2, 0.3)


def land(v):
    """Hitting the ground below, hard."""
    d = 0.9
    boom = sine(slide(80, 35, d), d) * env(d, 0.001, 0.15, 0)
    crunch = bp(noise(0.3), 200, 2500) * env(0.3, 0.001, 0.05, 0) * 0.8
    debris = mix(*[at(bp(noise(0.03), 1500, 6000) * env(0.03, 0.001, 0.008, 0) * RNG.uniform(0.1, 0.3), RNG.uniform(0.08, 0.5))
                 for _ in range(14)])
    return room(mix(boom, crunch, debris), 0.35, 1.2)


def click(v):
    """Interface tick."""
    d = 0.06
    return sine(slide(1800 + v * 200, 1200, d), d) * env(d, 0.001, 0.01, 0) * 0.6 + hp(noise(d), 4000) * env(d, 0.0005, 0.003, 0) * 0.2


# ---------------------------------------------------------- loops

def ambience(v):
    """The dungeon breathing: a low draught, distant groans, and drips."""
    d = 34.0
    t = tt(d)
    air = lp(brown(d), 260) * (0.75 + 0.25 * np.sin(2 * np.pi * t / 11.3)) * 1.6
    hum = (sine(48.5, d) * 0.06 + sine(73.0, d) * 0.03) * (0.6 + 0.4 * np.sin(2 * np.pi * t / 7.7))
    out = mix(air, hum)
    # drips: a few, quiet, mostly far off, with only a little room on them
    drips = np.zeros(n_(d))
    for k in range(int(d * 0.3)):
        t0 = RNG.uniform(0, d - 0.6)
        f = RNG.uniform(900, 2000)
        dd = 0.08
        far = RNG.uniform() < 0.6
        dr = sine(slide(f, f * 1.6, dd), dd) * env(dd, 0.001, 0.015, 0) * RNG.uniform(0.025, 0.07)
        if far:
            dr = lp(dr, 1400) * 0.6
        i = n_(t0)
        drips[i:i + len(dr)] += dr[:len(drips) - i]
    wet = fftconvolve(drips, room_ir())[:len(drips)] * 0.5
    out = mix(out, drips * 0.8 + lp(wet, 2500) * 0.35)
    # a far-off groan of settling stone every so often
    for t0 in (6.5, 19.0, 28.5):
        dd = 3.2
        g = lp(brown(dd), 120) * np.sin(np.pi * tt(dd) / dd) ** 2 * 0.9
        i = n_(t0)
        out[i:i + len(g)] += g[:len(out) - i]
    return loopable(out, 2.0)


def fire_loop(v, size=1.0):
    """A burning torch: the roar of the flame and pitch crackling."""
    d = 9.0
    t = tt(d)
    roar = lp(brown(d), 380 * size) * (0.8 + 0.2 * np.sin(2 * np.pi * t * 0.7) * np.sin(2 * np.pi * t * 1.9)) * 0.9
    breath = bp(noise(d), 500, 1600) * 0.06 * (0.6 + 0.4 * np.sin(2 * np.pi * t * 0.43))
    pops = crackles(d, 26 * size, 1200, 7000) * 2.2
    pops += crackles(d, 5, 300, 1500, 0.01) * 1.4
    return loopable(lp(mix(roar * 1.6, breath, pops), 7000, 2), 1.0)


def water_loop(v):
    """Still water lapping at stone."""
    d = 10.0
    t = tt(d)
    lap = lp(noise(d), 450, 4) * (0.5 + 0.5 * np.sin(2 * np.pi * t * 0.31) ** 2) * 3.0
    plinks = np.zeros(n_(d))
    for k in range(int(d * 3)):
        t0 = RNG.uniform(0, d - 0.2)
        f = RNG.uniform(500, 1400)
        dd = 0.07
        p = sine(slide(f, f * 1.5, dd), dd) * env(dd, 0.003, 0.02, 0) * RNG.uniform(0.05, 0.25)
        i = n_(t0)
        plinks[i:i + len(p)] += p[:len(plinks) - i]
    return loopable(mix(lap, plinks), 1.0)


# name -> (function, variants, loop)
SOUNDS = {
    "step": (step, 4, False), "wade": (wade, 3, False), "swim": (swim, 2, False), "splash": (splash, 2, False),
    "swing": (swing, 3, False), "hit": (hit, 3, False), "hurt": (hurt, 2, False), "die": (die, 1, False),
    "kill": (kill, 2, False), "rat": (rat, 3, False), "goblin": (goblin, 3, False), "shade": (shade, 3, False),
    "burn": (burn, 2, False), "dissolve": (dissolve, 1, False), "alert": (alert, 1, False),
    "ignite": (ignite, 2, False), "sconce": (sconce, 1, False), "snuff": (snuff, 2, False), "hiss": (hiss, 1, False),
    "throw": (throw, 2, False), "clink": (clink, 3, False), "thud": (thud, 2, False), "shatter": (shatter, 1, False),
    "drink": (drink, 1, False), "pickup": (pickup, 2, False), "door": (door, 3, False), "stairs": (stairs, 1, False),
    "fall": (fall, 1, False), "land": (land, 1, False), "click": (click, 1, False),
    "bat": (bat, 2, False), "ogre": (ogre, 2, False), "eel": (eel, 2, False), "sling": (sling, 2, False),
    "ambience": (ambience, 1, True), "torch_loop": (lambda v: fire_loop(v, 1.0), 1, True),
    "fire_loop": (lambda v: fire_loop(v, 0.8), 1, True), "water_loop": (water_loop, 1, True),
}

# event volume (0..1) for sounds.txt
VOLUME = {
    "step": 0.22, "wade": 0.35, "swim": 0.45, "splash": 0.55, "swing": 0.35, "hit": 0.55, "hurt": 0.6, "die": 0.7,
    "kill": 0.5, "rat": 0.35, "goblin": 0.45, "shade": 0.5, "burn": 0.5, "dissolve": 0.55, "alert": 0.45,
    "ignite": 0.5, "sconce": 0.55, "snuff": 0.4, "hiss": 0.45, "throw": 0.35, "clink": 0.4, "thud": 0.45,
    "shatter": 0.5, "drink": 0.5, "pickup": 0.35, "door": 0.45, "stairs": 0.55, "fall": 0.55, "land": 0.65,
    "bat": 0.35, "ogre": 0.6, "eel": 0.55, "sling": 0.4,
    "click": 0.25, "ambience": 0.5, "torch_loop": 0.35, "fire_loop": 0.3, "water_loop": 0.3,
}


def filename(name, v, n):
    return f"{name}.wav" if n == 1 else f"{name}_{'abcdefgh'[v]}.wav"


def main(argv):
    out = "assets/sounds"
    only = None
    args = argv[1:]
    force = "--force" in args
    args = [a for a in args if a != "--force"]
    if "--only" in args:
        i = args.index("--only")
        only = set(args[i + 1:])
        args = args[:i]
    if args:
        out = args[0]
    if "-h" in argv or "--help" in argv:
        print(__doc__)
        return 0
    if "-V" in argv or "--version" in argv:
        print(f"make_sounds {VERSION}")
        return 0
    os.makedirs(out, exist_ok=True)
    for name, (fn, n, loop) in SOUNDS.items():
        if only and name not in only:
            continue
        for v in range(n):
            path = os.path.join(out, filename(name, v, n))
            if os.path.exists(path) and not force and not only:
                print(f"  {path}  kept")
                continue
            x = fn(v)
            (write_loop if loop else write)(path, x)
            print(f"  {path}  {len(x) / SR:.2f}s")
    txt = os.path.join(out, "sounds.txt")
    if not os.path.exists(txt):
        with open(txt, "w") as f:
            f.write("# Which files each game event plays: event  file[,file...]  volume\n")
            f.write("# Several files means one is picked at random each time. Edit freely;\n")
            f.write("# tools/make_sounds.py only writes this file when it is missing.\n")
            for name, (fn, n, loop) in SOUNDS.items():
                files = ",".join(filename(name, v, n) for v in range(n))
                f.write(f"{name:<11} {files:<44} {VOLUME[name]:.2f}\n")
        print(f"  {txt}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
