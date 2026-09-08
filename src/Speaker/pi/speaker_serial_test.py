#!/usr/bin/env python3
"""Loopback test for the daemon's UART transport, no aircraft needed.

Opens a pseudo-terminal pair, starts the daemon on the slave end as if it were the air
unit's UART, then plays the ground station through the master end: sends STATE, PLAY,
VOLUME and STOP frames and checks each comes back with a state reply. Also proves the
daemon ignores the remote controller's own SDK frames that share the datalink.

Runs on Linux only (pty). Makes no sound: it points the daemon at an empty audio dir.

    python3 speaker_serial_test.py
"""

from __future__ import annotations

import os
import pty
import select
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import speaker_daemon as proto  # noqa: E402


def read_reply(fd: int, timeout: float = 2.0) -> list[tuple[int, int, bytes]]:
    buffer = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.1)
        if ready:
            buffer.extend(os.read(fd, 512))
            frames = proto.decode(buffer)
            if frames:
                return frames
    return []


def main() -> int:
    master, slave = pty.openpty()
    slave_path = os.ttyname(slave)
    audio_dir = tempfile.mkdtemp()
    (Path(audio_dir) / "01_dummy.mp3").write_bytes(b"\x00" * 64)

    env = dict(
        os.environ,
        SPEAKER_SERIAL=slave_path,
        SPEAKER_SERIAL_BAUD="57600",
        SPEAKER_AUDIO_DIR=audio_dir,
        SPEAKER_PORT="47270",
        SPEAKER_ALSA_DEVICE="null",  # ALSA null sink: PLAY spawns a player but nothing is heard
    )
    daemon = subprocess.Popen(
        [sys.executable, str(Path(__file__).with_name("speaker_daemon.py"))],
        env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
    )
    time.sleep(1.5)

    failures = 0

    def check(name: str, frame: bytes, expect_cmd: int | None) -> None:
        """expect_cmd: command id the reply must echo, or None when silence is correct."""
        nonlocal failures
        os.write(master, frame)
        frames = read_reply(master)
        expect_reply = expect_cmd is not None
        if expect_reply:
            ok = bool(frames) and frames[-1][0] == expect_cmd and len(frames[-1][2]) == 4
            state = frames[-1][2] if frames else b""
            detail = f"playing={state[0]} track={state[1]} volume={state[2]} tracks={state[3]}" if len(state) == 4 else "(no state)"
        else:
            ok = not frames
            detail = "silence" if ok else f"unexpected reply {frames}"
        print(f"  {'OK ' if ok else 'FAIL'} {name:26s} {detail}")
        failures += 0 if ok else 1

    print(f"daemon on {slave_path} (pty), driving it from the master end")
    check("STATE", proto.encode(proto.CMD_REQUEST_STATE), proto.CMD_REQUEST_STATE)
    check("PLAY 1", proto.encode(proto.CMD_PLAY, b"\x01"), proto.CMD_PLAY)
    check("VOLUME 55", proto.encode(proto.CMD_SET_VOLUME, b"\x37"), proto.CMD_SET_VOLUME)
    check("STOP", proto.encode(proto.CMD_STOP), proto.CMD_STOP)
    # The controller's SDK answers 0x42 channel data on the same datalink; we must not.
    sdk_channel_frame = proto.STX + b"\x00" + struct.pack("<HH", 32, 9) + b"\x42" + b"\xdc\x05" * 16
    sdk_channel_frame += struct.pack("<H", proto.crc16_xmodem(sdk_channel_frame))
    check("RC SDK 0x42 (ignore)", sdk_channel_frame, None)
    # Garbage between frames must not desync the parser.
    check("STATE after noise", b"\xff\x00\x55\x12" + proto.encode(proto.CMD_REQUEST_STATE), proto.CMD_REQUEST_STATE)

    daemon.terminate()
    try:
        out = daemon.communicate(timeout=3)[0]
    except subprocess.TimeoutExpired:
        daemon.kill()
        out = daemon.communicate()[0]
    serial_line = next((l for l in out.splitlines() if "serial on" in l or "serial" in l and "unavailable" in l), "")
    print(f"  daemon log: {serial_line.strip() or '(no serial line)'}")

    print("\nALL PASSED" if failures == 0 else f"\n{failures} FAILED")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
