#!/usr/bin/env python3
"""Loudspeaker payload daemon for the police drone.

Runs on the aircraft's small Linux computer (Raspberry Pi 4B here; the same file runs on a
Pi Zero 2 W or a Radxa) and drives the loudspeaker two ways:

  * Stored warnings. The ground station sends a track number; the audio itself lives
    here, so a broadcast already under way is unaffected by a link drop. This is the
    reliable path for fixed warnings.

  * Live microphone. The ground station streams raw PCM to the audio port and the daemon
    plays it as it arrives. A live stream takes priority over a stored track. (Not used
    in the current build - the receive side is kept so it can be enabled later.)

Commands arrive on either of two transports, handled identically:

  * UART from the SIYI air unit (production). The ground station sends to the SIYI
    datalink (UDP 192.168.144.20:19856 on the controller), which carries the bytes over
    RF and out of the air unit's UART1 into the Pi's GPIO serial port. Replies go back
    the same way. Measured air unit UART rate: 57600.

  * UDP on the control port (bench). Lets a laptop on the same LAN drive the daemon
    without any aircraft hardware.

Wire format is the SIYI packet framing the aircraft already uses for the camera:

    0x55 0x66 | CTRL(1) | DATA_LEN(2, LE) | SEQ(2, LE) | CMD_ID(1) | DATA | CRC16(2, LE)

CRC is CRC-16/XMODEM (poly 0x1021, init 0). The SIYI remote controller speaks the same
framing for its own SDK on the same datalink, so the daemon ignores command ids it does
not own rather than answering them.

Install as a systemd service so it survives a reboot:

    sudo cp speaker_daemon.py /usr/local/bin/
    sudo cp speaker.service /etc/systemd/system/
    sudo systemctl enable --now speaker
"""

from __future__ import annotations

import logging
import os
import re
import shutil
import signal
import socket
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

try:
    import serial  # pyserial: apt install python3-serial
except ImportError:  # bench machines without it still get the UDP transport
    serial = None

AUDIO_DIR = Path(os.environ.get("SPEAKER_AUDIO_DIR", "/opt/speaker/audio"))
CONTROL_PORT = int(os.environ.get("SPEAKER_PORT", "37270"))
# Live microphone PCM arrives one port above the control channel.
AUDIO_PORT = int(os.environ.get("SPEAKER_AUDIO_PORT", str(CONTROL_PORT + 1)))

# Serial transport from the air unit. Empty disables it. /dev/serial0 is whichever UART
# the Pi has routed to GPIO14/15, so it works whether or not Bluetooth was moved off the
# PL011. 57600 is what the SIYI air unit's UART1 was measured at (SDK 0x16, Com1_Baud=3).
SERIAL_PORT = os.environ.get("SPEAKER_SERIAL", "/dev/serial0")
SERIAL_BAUD = int(os.environ.get("SPEAKER_SERIAL_BAUD", "57600"))

# The ground station streams 16 kHz / 16-bit / mono. Speech is clear at this rate and it
# is only 256 kbps, which the SIYI link carries without touching the video budget.
AUDIO_RATE = int(os.environ.get("SPEAKER_AUDIO_RATE", "16000"))

# A broadcast keeps playing when the link drops, but not forever: an aircraft that flies
# away still shouting is worse than one that goes quiet. Refreshed by any command.
DEADMAN_SECONDS = float(os.environ.get("SPEAKER_DEADMAN_SECONDS", "120"))

# The air unit's UDP telemetry session drops without traffic, so the serial transport sends
# an unsolicited state frame this often to keep the ground->aircraft direction open. Only used
# by the UART transport; the UDP (bench) transport needs no heartbeat.
SERIAL_HEARTBEAT_SECONDS = float(os.environ.get("SPEAKER_SERIAL_HEARTBEAT_SECONDS", "0.25"))

# The live stream ends when the operator lets go of push-to-talk, which just stops the
# packets. Close the pipe after this long a gap so the tail of a word is not clipped but
# the speaker does not sit open hissing.
AUDIO_IDLE_SECONDS = float(os.environ.get("SPEAKER_AUDIO_IDLE_SECONDS", "0.4"))

STX = b"\xa5\x5a"
AUDIO_MAGIC = b"\xa5\x5b"  # one above STX. NOT 0x55 0x66: on the SIYI datalink the controller's
                          # RC MCU treats a 0x55 0x66 frame as its own SDK command and eats it
                          # instead of forwarding it to the air unit, so the payload never hears it
AUDIO_HEADER_LEN = 4  # magic(2) + sequence(2)
HEADER_LEN = 8  # STX(2) + CTRL(1) + LEN(2) + SEQ(2) + CMD(1)
CRC_LEN = 2
# The datalink can carry the controller's own SDK chatter; never let junk pile up.
MAX_BUFFER = 8192

CMD_PLAY = 0x01
CMD_STOP = 0x02
CMD_SET_VOLUME = 0x03
CMD_REQUEST_STATE = 0x04
OWN_COMMANDS = {CMD_PLAY, CMD_STOP, CMD_SET_VOLUME, CMD_REQUEST_STATE}

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
    if len(buffer) > MAX_BUFFER:
        del buffer[:-MAX_BUFFER]
    return frames


def find_usb_alsa_device() -> str | None:
    """ALSA device string for the USB speaker, or None to let ALSA pick the default.

    The USB speaker enumerates as a different card number on different boards (and can even
    move between boots), so it is found by the word USB in `aplay -l` rather than a fixed
    index. SPEAKER_ALSA_DEVICE overrides this when a board needs a hand-picked device.
    """
    override = os.environ.get("SPEAKER_ALSA_DEVICE")
    if override:
        return override
    if not shutil.which("aplay"):
        return None
    try:
        listing = subprocess.run(
            ["aplay", "-l"], capture_output=True, text=True, timeout=5
        ).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    for line in listing.splitlines():
        match = re.match(r"card (\d+):", line)
        if match and "usb" in line.lower():
            # plughw, not hw: it resamples/reformats so a file at some other rate still plays.
            return f"plughw:{match.group(1)},0"
    return None


class Player:
    """Plays one stored file at a time. Starting a new track replaces whatever is playing."""

    def __init__(self, audio_dir: Path, device: str | None) -> None:
        self._audio_dir = audio_dir
        self._device = device
        self._process: subprocess.Popen | None = None
        self._track = 0
        self._volume = 80
        self._lock = threading.Lock()
        self._player_cmd = self._find_player()

    @staticmethod
    def _find_player() -> str | None:
        for candidate in ("mpg123", "aplay", "ffplay"):
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

    def _argv(self, path: Path) -> list[str]:
        if self._player_cmd == "mpg123":
            argv = ["mpg123", "-q", "-f", str(int(self._volume * 327.68))]
            if self._device:
                argv += ["-o", "alsa", "-a", self._device]
            return argv + [str(path)]
        if self._player_cmd == "aplay":
            argv = ["aplay", "-q"]
            if self._device:
                argv += ["-D", self._device]
            return argv + [str(path)]
        # ffplay has no simple device flag; it falls back to the ALSA default.
        return ["ffplay", "-nodisp", "-autoexit", "-loglevel", "quiet", str(path)]

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
            log.info("playing track %d: %s", track, path.name)
            self._process = subprocess.Popen(
                self._argv(path), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
            )
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
        # amixer is best effort: the exact control name varies by USB device and board, and a
        # bench machine may have no alsa-utils at all. File playback still honours the level
        # through mpg123's own gain, so a missing mixer must not take the daemon down.
        if shutil.which("amixer") is None:
            return
        for control in ("PCM", "Master", "Speaker"):
            try:
                result = subprocess.run(
                    ["amixer", "sset", control, f"{self._volume}%"],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                )
            except OSError:
                return
            if result.returncode == 0:
                break

    def playing(self) -> bool:
        with self._lock:
            alive = self._process is not None and self._process.poll() is None
            if not alive:
                self._track = 0
            return alive

    def state(self) -> bytes:
        playing = self.playing()
        return bytes([1 if playing else 0, self._track, self._volume, min(255, len(self.tracks()))])


class LiveAudio:
    """Plays the ground station's live microphone stream through a held-open aplay pipe.

    Push-to-talk sends a burst of PCM packets and then stops; there is no explicit end
    marker, so the pipe is torn down once the packets stop arriving (see stop_if_idle).
    """

    def __init__(self, device: str | None, rate: int) -> None:
        self._device = device
        self._rate = rate
        self._process: subprocess.Popen | None = None
        self._last_packet = 0.0
        self._lock = threading.Lock()

    def active(self) -> bool:
        with self._lock:
            return self._process is not None and self._process.poll() is None

    def feed(self, pcm: bytes) -> None:
        if not pcm:
            return
        with self._lock:
            if self._process is None or self._process.poll() is not None:
                self._start_locked()
            # aplay missing or failed to launch: already logged, drop the packet rather than
            # let the receive thread die and take live audio down for good.
            if self._process is None or self._process.stdin is None:
                return
            self._last_packet = time.monotonic()
            try:
                self._process.stdin.write(pcm)
                self._process.stdin.flush()
            except (BrokenPipeError, OSError) as exc:
                log.warning("live audio pipe broke: %s", exc)
                self._stop_locked()

    def stop_if_idle(self, idle_seconds: float) -> None:
        with self._lock:
            if self._process is not None and time.monotonic() - self._last_packet > idle_seconds:
                log.info("live audio idle, closing")
                self._stop_locked()

    def stop(self) -> None:
        with self._lock:
            self._stop_locked()

    def _start_locked(self) -> None:
        if shutil.which("aplay") is None:
            # Once, not once per 20 ms packet: a missing aplay would otherwise flood the journal.
            if not getattr(self, "_warned_no_aplay", False):
                log.error("aplay not found; install alsa-utils for live audio")
                self._warned_no_aplay = True
            return
        argv = ["aplay", "-q", "-t", "raw", "-f", "S16_LE", "-r", str(self._rate), "-c", "1"]
        if self._device:
            argv += ["-D", self._device]
        argv += ["-"]
        log.info("live audio starting")
        self._process = subprocess.Popen(
            argv, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )

    def _stop_locked(self) -> None:
        if self._process is None:
            return
        try:
            if self._process.stdin is not None:
                self._process.stdin.close()
        except OSError:
            pass
        if self._process.poll() is None:
            self._process.terminate()
            try:
                self._process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                self._process.kill()
        self._process = None


class Commands:
    """Turns decoded frames into player actions and state replies, from any transport.

    Both the UART and the UDP receiver call handle(); the reply goes back on whichever
    transport the command came from. Everything the deadman needs is kept here so the
    two transports refresh the same timer.
    """

    def __init__(self, player: Player, live: LiveAudio) -> None:
        self._player = player
        self._live = live
        self._lock = threading.Lock()
        self._sequence = 0
        self.last_command = time.monotonic()

    def state_frame(self) -> bytes:
        """A state frame for the heartbeat, sequence-numbered like a reply."""
        with self._lock:
            self._sequence = (self._sequence + 1) & 0xFFFF
            return encode(CMD_REQUEST_STATE, self._player.state(), self._sequence)

    def handle(self, command_id: int, payload: bytes) -> bytes | None:
        """Reply frame for a command, or None for a frame that is not ours (e.g. RC SDK)."""
        if command_id not in OWN_COMMANDS:
            return None
        with self._lock:
            self.last_command = time.monotonic()
            if command_id == CMD_PLAY and payload:
                self._live.stop()  # an explicit file request wins over a live stream
                self._player.play(payload[0])
            elif command_id == CMD_STOP:
                self._live.stop()
                self._player.stop()
            elif command_id == CMD_SET_VOLUME and payload:
                self._player.set_volume(payload[0])
            self._sequence = (self._sequence + 1) & 0xFFFF
            return encode(command_id, self._player.state(), self._sequence)


def _audio_loop(sock: socket.socket, live: LiveAudio, player: Player, running: threading.Event) -> None:
    """Receive live PCM and play it, cutting in over any stored track."""
    sock.settimeout(0.2)
    while running.is_set():
        try:
            datagram, _sender = sock.recvfrom(4096)
        except socket.timeout:
            live.stop_if_idle(AUDIO_IDLE_SECONDS)
            continue
        except OSError as exc:
            log.error("audio socket error: %s", exc)
            continue

        if len(datagram) <= AUDIO_HEADER_LEN or datagram[:2] != AUDIO_MAGIC:
            continue
        # First packet of a burst: silence the stored track so the two do not overlap.
        if not live.active():
            player.stop()
        live.feed(datagram[AUDIO_HEADER_LEN:])


def _serial_loop(port: str, baud: int, commands: Commands, running: threading.Event) -> None:
    """Commands from the air unit's UART; replies back up the same wire.

    The air unit's second telemetry channel is set to UDP, which only carries the ground
    station's commands down to us while its UDP session with the controller is live, and that
    session only stays up while bytes flow the other way. A daemon that spoke only in reply
    would never get a first command to reply to, so it heartbeats its state up the wire about
    once a second. The ground station already polls state, so the extra frames are harmless.
    """
    try:
        link = serial.Serial(port, baud, timeout=0.2, write_timeout=1.0)
    except (OSError, serial.SerialException) as exc:
        log.error("serial %s unavailable, UART transport off: %s", port, exc)
        return
    log.info("serial on %s @ %d", port, baud)

    buffer = bytearray()
    last_heartbeat = 0.0
    while running.is_set():
        now = time.monotonic()
        if now - last_heartbeat >= SERIAL_HEARTBEAT_SECONDS:
            last_heartbeat = now
            try:
                link.write(commands.state_frame())
            except (OSError, serial.SerialException) as exc:
                log.warning("serial heartbeat failed: %s", exc)

        try:
            chunk = link.read(512)
        except (OSError, serial.SerialException) as exc:
            log.error("serial read failed: %s", exc)
            time.sleep(1.0)
            continue
        if not chunk:
            continue
        buffer.extend(chunk)
        for command_id, _seq, payload in decode(buffer):
            reply = commands.handle(command_id, payload)
            if reply is None:
                continue
            try:
                link.write(reply)
            except (OSError, serial.SerialException) as exc:
                log.warning("serial write failed: %s", exc)
    link.close()


def main() -> int:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
        stream=sys.stdout,
    )

    device = find_usb_alsa_device()
    log.info("audio device: %s", device or "(ALSA default)")

    player = Player(AUDIO_DIR, device)
    live = LiveAudio(device, AUDIO_RATE)
    commands = Commands(player, live)
    log.info("audio dir %s (%d files), player=%s", AUDIO_DIR, len(player.tracks()), player._player_cmd)

    control_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    control_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    control_sock.bind(("0.0.0.0", CONTROL_PORT))
    control_sock.settimeout(1.0)
    log.info("control on udp/%d", CONTROL_PORT)

    audio_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    audio_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    audio_sock.bind(("0.0.0.0", AUDIO_PORT))
    log.info("live audio on udp/%d (%d Hz)", AUDIO_PORT, AUDIO_RATE)

    running = threading.Event()
    running.set()

    threads = [threading.Thread(target=_audio_loop, args=(audio_sock, live, player, running), daemon=True)]
    if SERIAL_PORT and serial is not None:
        threads.append(threading.Thread(target=_serial_loop, args=(SERIAL_PORT, SERIAL_BAUD, commands, running), daemon=True))
    elif SERIAL_PORT:
        log.error("pyserial not installed (apt install python3-serial); UART transport off")
    else:
        log.info("serial transport disabled")
    for thread in threads:
        thread.start()

    def shutdown(*_args: object) -> None:
        running.clear()

    signal.signal(signal.SIGTERM, shutdown)
    signal.signal(signal.SIGINT, shutdown)

    buffer = bytearray()

    while running.is_set():
        try:
            datagram, sender = control_sock.recvfrom(2048)
            buffer.extend(datagram)
        except socket.timeout:
            # Stop a broadcast that outlives contact with the ground station. Live audio
            # stops on its own the moment push-to-talk is released, so this only guards
            # stored tracks.
            if time.monotonic() - commands.last_command > DEADMAN_SECONDS and player.playing():
                log.warning("deadman expired after %.0fs, stopping playback", DEADMAN_SECONDS)
                player.stop()
            continue
        except OSError as exc:
            log.error("control socket error: %s", exc)
            continue

        for command_id, _seq, payload in decode(buffer):
            reply = commands.handle(command_id, payload)
            if reply is not None:
                control_sock.sendto(reply, sender)

    running.clear()
    for thread in threads:
        thread.join(timeout=2)
    live.stop()
    player.stop()
    control_sock.close()
    audio_sock.close()
    log.info("stopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
