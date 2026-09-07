#!/usr/bin/env python3
"""Bench tool: drive the speaker daemon from a laptop without the ground station.

Sends the same packets the GCS SpeakerController sends, so the daemon can be checked on
the desk before the app side exists. Needs only the Python standard library.

    speaker_test_send.py --host 192.168.144.70 state
    speaker_test_send.py --host 192.168.144.70 play 2
    speaker_test_send.py --host 192.168.144.70 volume 60
    speaker_test_send.py --host 192.168.144.70 stop
    speaker_test_send.py --host 192.168.144.70 mic announcement.wav

`mic` streams a WAV file to the live audio port at real-time pace, exactly as push-to-talk
will. The file must be 16 kHz / 16-bit / mono (convert with:
`ffmpeg -i in.mp3 -ar 16000 -ac 1 -sample_fmt s16 out.wav`).
"""

from __future__ import annotations

import argparse
import socket
import struct
import sys
import time
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import speaker_daemon as proto  # noqa: E402  (same directory)

CHUNK_MS = 20


def control(host: str, port: int, command: int, payload: bytes = b"", wait_reply: bool = True) -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(2.0)
    # The GCS sends every command twice; UDP on the RF link drops the odd packet.
    for seq in (1, 2):
        sock.sendto(proto.encode(command, payload, seq), (host, port))
    if not wait_reply:
        return
    try:
        data, _ = sock.recvfrom(2048)
    except socket.timeout:
        print("no reply (daemon down, wrong host, or link)")
        return
    frames = proto.decode(bytearray(data))
    if not frames:
        print("reply did not decode:", data.hex())
        return
    _cmd, _seq, state = frames[-1]
    if len(state) >= 4:
        playing, track, volume, count = state[:4]
        print(f"playing={bool(playing)} track={track} volume={volume}% tracks={count}")
    else:
        print("state payload:", state.hex())


def stream_wav(host: str, port: int, path: Path, rate: int) -> None:
    with wave.open(str(path), "rb") as wav:
        if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) != (1, 2, rate):
            sys.exit(
                f"{path}: need mono / 16-bit / {rate} Hz, got "
                f"{wav.getnchannels()}ch / {wav.getsampwidth()*8}-bit / {wav.getframerate()} Hz"
            )
        frames_per_chunk = rate * CHUNK_MS // 1000
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        seq = 0
        started = time.monotonic()
        sent = 0
        while True:
            pcm = wav.readframes(frames_per_chunk)
            if not pcm:
                break
            sock.sendto(proto.AUDIO_MAGIC + struct.pack("<H", seq & 0xFFFF) + pcm, (host, port))
            seq += 1
            sent += len(pcm)
            # Pace to real time so the daemon's pipe never runs dry or overflows.
            target = started + seq * CHUNK_MS / 1000
            delay = target - time.monotonic()
            if delay > 0:
                time.sleep(delay)
        print(f"streamed {sent} bytes in {seq} packets ({seq * CHUNK_MS / 1000:.1f} s)")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default=proto.os.environ.get("SPEAKER_HOST", "192.168.144.70"))
    ap.add_argument("--port", type=int, default=proto.CONTROL_PORT)
    ap.add_argument("--audio-port", type=int, default=None, help="default: control port + 1")
    ap.add_argument("--rate", type=int, default=proto.AUDIO_RATE)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("state")
    sub.add_parser("stop")
    sub.add_parser("play").add_argument("track", type=int)
    sub.add_parser("volume").add_argument("percent", type=int)
    sub.add_parser("mic").add_argument("wav", type=Path)
    args = ap.parse_args()

    audio_port = args.audio_port if args.audio_port is not None else args.port + 1

    if args.cmd == "state":
        control(args.host, args.port, proto.CMD_REQUEST_STATE)
    elif args.cmd == "play":
        control(args.host, args.port, proto.CMD_PLAY, bytes([args.track]))
    elif args.cmd == "stop":
        control(args.host, args.port, proto.CMD_STOP)
    elif args.cmd == "volume":
        control(args.host, args.port, proto.CMD_SET_VOLUME, bytes([max(0, min(100, args.percent))]))
    elif args.cmd == "mic":
        stream_wav(args.host, audio_port, args.wav, args.rate)
    return 0


if __name__ == "__main__":
    sys.exit(main())
