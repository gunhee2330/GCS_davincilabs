#!/usr/bin/env python3
"""Synthesise the loudspeaker's siren track, so track 1 is reproducible like the TTS ones.

An airborne speaker fights the motors: their noise is broadband and strongest below ~2 kHz,
which is where speech lives, so the spoken announcements get buried in flight. A siren is a
harmonic-rich tone swept through 650-1450 Hz — the band where hearing is most sensitive and
above the blade-pass hum — written at full scale, so it carries where speech does not.

    pip install numpy lameenc
    python make_siren.py                # writes ./announcements/01_사이렌.mp3 (and .wav)
    scp announcements/01_사이렌.mp3 mrdev@PI:~/speaker/ && ssh mrdev@PI sudo mv ~/speaker/01_사이렌.mp3 /opt/speaker/audio/

The Jieli USB speaker only plays 32 kHz, so the file is written at that rate. The daemon plays
the mp3 through mpg123; the wav is there to audition on a PC.
"""

from __future__ import annotations

import sys
import wave
from pathlib import Path

import numpy as np

RATE = 32000
DURATION_S = 30.0
F_LOW_HZ = 650.0
F_HIGH_HZ = 1450.0
CYCLE_S = 1.8            # one rise and fall
RISE_FRACTION = 0.45     # the rise is quicker than the fall, like a rotor siren winding down
# Sawtooth-like harmonic weights. Overtones are what cut through broadband noise; a pure sine
# at the same level sounds much quieter next to the motors.
HARMONICS = (1.0, 0.55, 0.35, 0.25, 0.18, 0.12)
# tanh soft-clip drive. 8 squares the wave up to about -1.4 dBFS RMS, within 1.4 dB of the
# loudest any file can be; the rest is the speaker's amplifier. Lower it for a rounder tone.
DRIVE = 8.0
EDGE_S = 0.03            # fade at both ends so the start and the stop command do not click
PEAK = 0.98


def wail_frequency(t: np.ndarray) -> np.ndarray:
    """Instantaneous pitch: an exponential sweep up then down, which the ear hears as even."""
    phase = (t % CYCLE_S) / CYCLE_S
    position = np.where(
        phase < RISE_FRACTION,
        phase / RISE_FRACTION,
        1.0 - (phase - RISE_FRACTION) / (1.0 - RISE_FRACTION),
    )
    return F_LOW_HZ * (F_HIGH_HZ / F_LOW_HZ) ** position


def synthesise() -> np.ndarray:
    """Mono float samples in [-1, 1]."""
    t = np.arange(int(RATE * DURATION_S)) / RATE
    # Integrate the frequency so the phase is continuous through the sweep turnarounds.
    phase = 2.0 * np.pi * np.cumsum(wail_frequency(t)) / RATE
    signal = sum(weight * np.sin((k + 1) * phase) for k, weight in enumerate(HARMONICS))
    signal /= sum(HARMONICS)
    signal = np.tanh(DRIVE * signal) / np.tanh(DRIVE)

    edge = int(RATE * EDGE_S)
    ramp = 0.5 - 0.5 * np.cos(np.linspace(0.0, np.pi, edge))
    signal[:edge] *= ramp
    signal[-edge:] *= ramp[::-1]
    return signal * (PEAK / np.max(np.abs(signal)))


def to_pcm_stereo(signal: np.ndarray) -> np.ndarray:
    mono = np.clip(np.round(signal * 32767.0), -32768, 32767).astype(np.int16)
    return np.column_stack((mono, mono))


def write_wav(path: Path, pcm: np.ndarray) -> None:
    with wave.open(str(path), "wb") as out:
        out.setnchannels(pcm.shape[1])
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(pcm.tobytes())


def write_mp3(path: Path, pcm: np.ndarray) -> bool:
    try:
        import lameenc
    except ImportError:
        print("lameenc not installed; only the wav was written (pip install lameenc)")
        return False
    encoder = lameenc.Encoder()
    encoder.set_bit_rate(128)
    encoder.set_in_sample_rate(RATE)
    encoder.set_channels(pcm.shape[1])
    encoder.set_quality(2)
    path.write_bytes(encoder.encode(pcm.tobytes()) + encoder.flush())
    return True


def main() -> int:
    out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent / "announcements"
    out_dir.mkdir(parents=True, exist_ok=True)
    pcm = to_pcm_stereo(synthesise())
    stem = out_dir / "01_사이렌"
    write_wav(stem.with_suffix(".wav"), pcm)
    if write_mp3(stem.with_suffix(".mp3"), pcm):
        print(f"{stem.name}.mp3 {stem.with_suffix('.mp3').stat().st_size // 1024:4d} KB  {DURATION_S:.0f} s wail {F_LOW_HZ:.0f}-{F_HIGH_HZ:.0f} Hz")
    return 0


if __name__ == "__main__":
    sys.exit(main())
