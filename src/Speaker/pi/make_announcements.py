#!/usr/bin/env python3
"""Generate the loudspeaker's warning tracks with TTS, so the wording lives in code.

The spoken files that end up in /opt/speaker/audio on the aircraft are produced from the
sentences below. The number prefix is the track the ground station sends: track 1 is the siren
from make_siren.py (speech is drowned out by the motors in flight, a siren is not), and the
GCS button labels (설정 → 스피커 페이로드 → 방송 문구 이름) should stay in the same order.

Edit a sentence, rerun, copy the new file to the Pi. Nothing needs recording.

    pip install edge-tts
    python make_announcements.py            # writes ./announcements/02_… 04_….mp3
    scp announcements/*.mp3 mrdev@PI:~/speaker/ && ssh mrdev@PI sudo mv ~/speaker/*.mp3 /opt/speaker/audio/

Uses Microsoft Edge's neural Korean voices (free, needs internet while generating).
"""

from __future__ import annotations

import asyncio
import sys
from pathlib import Path

import edge_tts

# ko-KR-InJoonNeural: male, reads as an official announcement. ko-KR-SunHiNeural is the
# female alternative. Slightly slowed so it carries from altitude.
VOICE = "ko-KR-InJoonNeural"
RATE = "-5%"

# (file stem, spoken text). The stem's number prefix fixes the track order on the Pi, which
# sorts by name. Keep these matched to the GCS default 방송 문구 이름 order:
#   사이렌, 위험 경고, 접근 금지, 경찰 안내
TRACKS = [
    ("02_위험경고", "위험합니다. 이 지역은 위험 구역입니다. 즉시 안전한 곳으로 대피하십시오."),
    ("03_접근금지", "경찰입니다. 이 지역은 접근이 금지되어 있습니다. 접근하지 마십시오."),
    ("04_경찰안내", "경찰 드론이 순찰 중입니다. 시민 여러분의 협조에 감사드립니다."),
]


async def generate(out_dir: Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    for stem, text in TRACKS:
        path = out_dir / f"{stem}.mp3"
        await edge_tts.Communicate(text, VOICE, rate=RATE).save(str(path))
        print(f"{path.name:20s} {path.stat().st_size // 1024:4d} KB  {text}")


def main() -> int:
    out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent / "announcements"
    asyncio.run(generate(out_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
