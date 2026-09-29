#!/usr/bin/env python3
"""Level the spoken announcements so they carry over the motors.

edge-tts writes speech at a cautious level - peaks near -4 dBFS, an average around -18 dBFS -
with most of its energy below 1 kHz, which is exactly where a multirotor's noise sits. Played
from the aircraft in flight that is inaudible. This pass drops what the small speaker cannot
reproduce anyway, lifts the 1.5-4 kHz band that carries the consonants, compresses the dynamics
so the quiet syllables come up to the loud ones, and limits the result to just under full
scale. The voice is the same; it is only louder and clearer. Expect about +10 dB of average
level.

    pip install numpy scipy lameenc
    python boost_announcements.py -o boosted announcements/02_*.mp3 announcements/03_*.mp3 ...

Output files keep their names, so they drop straight into /opt/speaker/audio in place of the
originals. mp3 input needs mpg123 or ffmpeg on the path to decode; wav input needs nothing.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

import numpy as np
from scipy import signal

HIGHPASS_HZ = 120.0
PRESENCE_HZ = 2500.0
PRESENCE_DB = 5.0
PRESENCE_Q = 0.9
COMP_THRESHOLD_DB = -26.0
COMP_RATIO = 4.0
COMP_ATTACK_S = 0.002
COMP_RELEASE_S = 0.10
TARGET_ACTIVE_RMS_DB = -8.0   # average level of the parts that are not pauses, before limiting
LIMIT_DRIVE = 2.0             # tanh limiter drive; higher squashes peaks harder
PEAK = 0.98
ACTIVE_GATE_DB = -40.0        # frames below this are pauses and do not count as speech


def read_audio(path: Path) -> tuple[np.ndarray, int]:
    """Mono float samples and the sample rate, decoding mp3 through mpg123 or ffmpeg."""
    if path.suffix.lower() == ".wav":
        return read_wav(path)
    with tempfile.TemporaryDirectory() as tmp:
        wav = Path(tmp) / "decoded.wav"
        if shutil.which("mpg123"):
            subprocess.run(["mpg123", "-q", "-w", str(wav), str(path)], check=True)
        elif shutil.which("ffmpeg"):
            subprocess.run(["ffmpeg", "-loglevel", "quiet", "-y", "-i", str(path), str(wav)], check=True)
        else:
            raise SystemExit(f"{path.name}: no mpg123 or ffmpeg to decode mp3; give a wav instead")
        return read_wav(wav)


def read_wav(path: Path) -> tuple[np.ndarray, int]:
    with wave.open(str(path), "rb") as src:
        if src.getsampwidth() != 2:
            raise SystemExit(f"{path.name}: only 16-bit wav is handled")
        pcm = np.frombuffer(src.readframes(src.getnframes()), dtype=np.int16)
        channels = src.getnchannels()
        rate = src.getframerate()
    return pcm.reshape(-1, channels).astype(np.float64).mean(axis=1) / 32768.0, rate


def peaking_eq(x: np.ndarray, rate: int, f0: float, gain_db: float, q: float) -> np.ndarray:
    """RBJ cookbook peaking filter."""
    a = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * np.pi * f0 / rate
    alpha = np.sin(w0) / (2.0 * q)
    b = np.array([1.0 + alpha * a, -2.0 * np.cos(w0), 1.0 - alpha * a])
    den = np.array([1.0 + alpha / a, -2.0 * np.cos(w0), 1.0 - alpha / a])
    return signal.lfilter(b / den[0], den / den[0], x)


def compress(x: np.ndarray, rate: int) -> np.ndarray:
    """Feed-forward compressor on a peak envelope; gain is computed in dB above the threshold."""
    attack = np.exp(-1.0 / (COMP_ATTACK_S * rate))
    release = np.exp(-1.0 / (COMP_RELEASE_S * rate))
    envelope = np.empty_like(x)
    level = 0.0
    for i, sample in enumerate(np.abs(x)):
        coefficient = attack if sample > level else release
        level = coefficient * level + (1.0 - coefficient) * sample
        envelope[i] = level
    level_db = 20.0 * np.log10(np.maximum(envelope, 1e-6))
    over = np.maximum(level_db - COMP_THRESHOLD_DB, 0.0)
    gain_db = -over * (1.0 - 1.0 / COMP_RATIO)
    return x * (10.0 ** (gain_db / 20.0))


def active_rms_db(x: np.ndarray, rate: int) -> float:
    frame = int(rate * 0.05)
    frames = x[: len(x) // frame * frame].reshape(-1, frame)
    rms = np.sqrt((frames ** 2).mean(axis=1))
    speech = rms[rms > 10.0 ** (ACTIVE_GATE_DB / 20.0)]
    return float(20.0 * np.log10(np.sqrt((speech ** 2).mean()))) if len(speech) else -120.0


def boost(x: np.ndarray, rate: int) -> np.ndarray:
    sos = signal.butter(2, HIGHPASS_HZ, "highpass", fs=rate, output="sos")
    y = signal.sosfilt(sos, x)
    y = peaking_eq(y, rate, PRESENCE_HZ, PRESENCE_DB, PRESENCE_Q)
    y = compress(y, rate)
    y *= 10.0 ** ((TARGET_ACTIVE_RMS_DB - active_rms_db(y, rate)) / 20.0)
    y = np.tanh(LIMIT_DRIVE * y) / np.tanh(LIMIT_DRIVE)
    return y * (PEAK / np.max(np.abs(y)))


def write_mp3(path: Path, y: np.ndarray, rate: int) -> None:
    import lameenc

    pcm = np.clip(np.round(y * 32767.0), -32768, 32767).astype(np.int16)
    encoder = lameenc.Encoder()
    encoder.set_bit_rate(128)
    encoder.set_in_sample_rate(rate)
    encoder.set_channels(1)
    encoder.set_quality(2)
    path.write_bytes(encoder.encode(pcm.tobytes()) + encoder.flush())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("inputs", nargs="+", type=Path, help="mp3 or wav announcements")
    parser.add_argument("-o", "--out", type=Path, default=Path("boosted"), help="output directory")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    for source in args.inputs:
        x, rate = read_audio(source)
        y = boost(x, rate)
        target = args.out / (source.stem + ".mp3")
        write_mp3(target, y, rate)
        print(f"{target.name:22s} peak {20 * np.log10(np.abs(x).max()):5.1f} -> {20 * np.log10(np.abs(y).max()):5.1f} dBFS"
              f"   speech {active_rms_db(x, rate):6.1f} -> {active_rms_db(y, rate):6.1f} dBFS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
