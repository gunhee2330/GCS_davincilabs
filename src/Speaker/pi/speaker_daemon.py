#!/usr/bin/env python3
"""Loudspeaker payload daemon for the police drone.

Listens on UDP for the ground station's playback commands and plays the matching audio
file through the attached amplifier. The ground station only ever sends a track number;
the audio itself lives here, so a broadcast already under way is unaffected by a link
drop.

Wire format is the SIYI packet framing the aircraft already uses for the camera, so the
airframe carries one format rather than two:

    0x55 0x66 | CTRL(1) | DATA_LEN(2, LE) | SEQ(2, LE) | CMD_ID(1) | DATA | CRC16(2, LE)

CRC is CRC-16/XMODEM (poly 0x1021, init 0).

Install as a systemd service so it survives a reboot:

    sudo cp speaker_daemon.py /usr/local/bin/
    sudo cp speaker.service /etc/systemd/system/
    sudo systemctl enable --now speaker
"""

from __future__ import annotations

import logging
import os
import shutil
import signal
import socket
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

AUDIO_DIR = Path(os.environ.get("SPEAKER_AUDIO_DIR", "/opt/speaker/audio"))
LISTEN_PORT = int(os.environ.get("SPEAKER_PORT", "37270"))

# A broadcast keeps playing when the link drops, but not forever: an aircraft that flies
# away still shouting is worse than one that goes quiet. Refreshed by any command.
DEADMAN_SECONDS = float(os.environ.get("SPEAKER_DEADMAN_SECONDS", "120"))

STX = b"\x55\x66"
HEADER_LEN = 8  # STX(2) + CTRL(1) + LEN(2) + SEQ(2) + CMD(1)
CRC_LEN = 2

CMD_PLAY = 0x01
CMD_STOP = 0x02
CMD_SET_VOLUME = 0x03
CMD_REQUEST_STATE = 0x04

log = logging.getLogger("speaker")


def crc16_xmodem(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode(command_id: int, payload: bytes = b"", sequence: int = 0) -> bytes:
    packet = STX + bytes([0x02]) + struct.pack("<HH", len(payload), sequence) + bytes([command_id]) + payload
    return packet + struct.pack("<H", crc16_xmodem(packet))


def decode(buffer: bytearray) -> list[tuple[int, int, bytes]]:
    """Pull out every complete CRC-valid frame, consuming what it used.

    Returns (command_id, sequence, payload). A partial trailing frame stays in the buffer.
    """
    frames: list[tuple[int, int, bytes]] = []
    consumed = 0

    while len(buffer) - consumed >= HEADER_LEN + CRC_LEN:
        if buffer[consumed : consumed + 2] != STX:
            consumed += 1
            continue

        data_len, sequence = struct.unpack_from("<HH", buffer, consumed + 3)
        if data_len > 512:
            consumed += 1
            continue

        total = HEADER_LEN + data_len + CRC_LEN
        if len(buffer) - consumed < total:
            break  # rest has not arrived

        frame = bytes(buffer[consumed : consumed + total])
        received_crc = struct.unpack_from("<H", frame, total - CRC_LEN)[0]
        if crc16_xmodem(frame[: total - CRC_LEN]) != received_crc:
            consumed += 1
            continue

        frames.append((frame[7], sequence, frame[HEADER_LEN : HEADER_LEN + data_len]))
        consumed += total

    del buffer[:consumed]
    return frames


class Player:
    """Plays one file at a time. Starting a new track replaces whatever is playing."""

    def __init__(self, audio_dir: Path) -> None:
        self._audio_dir = audio_dir
        self._process: subprocess.Popen | None = None
        self._track = 0
        self._volume = 80
        self._lock = threading.Lock()
        self._player_cmd = self._find_player()

    @staticmethod
    def _find_player() -> str | None:
        for candidate in ("mpg123", "ffplay", "aplay"):
            if shutil.which(candidate):
                return candidate
        return None

    def tracks(self) -> list[Path]:
        if not self._audio_dir.is_dir():
            return []
        return sorted(
            p for p in self._audio_dir.iterdir()
            if p.suffix.lower() in {".mp3", ".wav", ".ogg", ".flac"}
        )

    def play(self, track: int) -> None:
        files = self.tracks()
        if not 1 <= track <= len(files):
            log.warning("track %d out of range (%d files)", track, len(files))
            return
        if self._player_cmd is None:
            log.error("no audio player found; install mpg123")
            return

        path = files[track - 1]
        with self._lock:
            self._terminate_locked()
            if self._player_cmd == "mpg123":
                argv = ["mpg123", "-q", "-f", str(int(self._volume * 327.68)), str(path)]
            elif self._player_cmd == "ffplay":
                argv = ["ffplay", "-nodisp", "-autoexit", "-loglevel", "quiet", str(path)]
            else:
                argv = ["aplay", "-q", str(path)]
            log.info("playing track %d: %s", track, path.name)
            self._process = subprocess.Popen(argv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            self._track = track

    def stop(self) -> None:
        with self._lock:
            if self._process is not None:
                log.info("stopping playback")
            self._terminate_locked()

    def _terminate_locked(self) -> None:
        if self._process is not None and self._process.poll() is None:
            self._process.terminate()
            try:
                self._process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self._process.kill()
        self._process = None
        self._track = 0

    def set_volume(self, percent: int) -> None:
        self._volume = max(0, min(100, percent))
        # amixer is best effort: the exact control name varies by audio HAT.
        for control in ("PCM", "Master", "Speaker"):
            if subprocess.run(
                ["amixer", "sset", control, f"{self._volume}%"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            ).returncode == 0:
                break

    def state(self) -> bytes:
        with self._lock:
            playing = self._process is not None and self._process.poll() is None
            if not playing:
                self._track = 0
        return bytes([1 if playing else 0, self._track, self._volume, min(255, len(self.tracks()))])


def main() -> int:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
        stream=sys.stdout,
    )

    player = Player(AUDIO_DIR)
    log.info("audio dir %s (%d files), player=%s", AUDIO_DIR, len(player.tracks()), player._player_cmd)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", LISTEN_PORT))
    sock.settimeout(1.0)
    log.info("listening on udp/%d", LISTEN_PORT)

    running = True

    def shutdown(*_args: object) -> None:
        nonlocal running
        running = False

    signal.signal(signal.SIGTERM, shutdown)
    signal.signal(signal.SIGINT, shutdown)

    buffer = bytearray()
    last_command = time.monotonic()
    sequence = 0

    while running:
        try:
            datagram, sender = sock.recvfrom(2048)
            buffer.extend(datagram)
        except socket.timeout:
            # Stop a broadcast that outlives contact with the ground station.
            if time.monotonic() - last_command > DEADMAN_SECONDS:
                if player.state()[0]:
                    log.warning("deadman expired after %.0fs, stopping playback", DEADMAN_SECONDS)
                    player.stop()
            continue
        except OSError as exc:
            log.error("socket error: %s", exc)
            continue

        for command_id, _seq, payload in decode(buffer):
            last_command = time.monotonic()

            if command_id == CMD_PLAY and payload:
                player.play(payload[0])
            elif command_id == CMD_STOP:
                player.stop()
            elif command_id == CMD_SET_VOLUME and payload:
                player.set_volume(payload[0])
            elif command_id != CMD_REQUEST_STATE:
                log.debug("unhandled command 0x%02x", command_id)
                continue

            sequence = (sequence + 1) & 0xFFFF
            sock.sendto(encode(command_id, player.state(), sequence), sender)

    player.stop()
    sock.close()
    log.info("stopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
