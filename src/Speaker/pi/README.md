# 기체 스피커 페이로드 — 라즈베리파이 4B 설치

지상국이 UDP로 트랙 번호를 보내면 이 데몬이 해당 파일을 재생하고, 조종자가 PTT를 누르면
마이크 음성을 실시간으로 받아 그대로 내보냅니다. **음성 파일은 기체에 있으므로** 이미 시작된
방송은 링크가 끊겨도 끝까지 나갑니다.

`speaker_daemon.py`는 순수 Python + 표준 리눅스 도구만 쓰므로 **Pi 4B · Pi Zero 2 W · Radxa Zero 3E
어디서나 같은 파일이 그대로 돕니다.** 보드마다 다른 건 아래 OS 설정뿐입니다.

## 하드웨어 (Pi 4B)

```
에어유닛 LAN ─[스위치]─랜선─── Pi 4B 이더넷 포트 (내장)
USB-C 스피커 ─(USB-C→USB-A 어댑터)─ Pi 4B USB-A 포트  ※ USB-C 포트는 전원 전용
                                     전원: 기체 5V 3A → Pi USB-C PWR
```

- Pi 4B는 **이더넷 내장 + USB-A 4개**라 허브·동글이 필요 없습니다. Zero 2 W를 쓰면 USB OTG 허브와
  USB 이더넷 어댑터가 추가로 필요합니다.
- **스피커는 반드시 USB-A 포트에.** Pi 4B의 USB-C는 전원 입력 전용이라 거기선 인식되지 않습니다.
- 스피커는 USB 오디오 장치(플러그앤플레이). 리눅스가 사운드카드로 잡습니다.
- Pi 4B는 5V **3A**를 요구합니다. 기체 5V 레일이 이걸 감당하는지 확인하세요 (부족하면 부팅 중 리셋).
- 고정 IP **192.168.144.70** — GCS `SpeakerProtocol::kDefaultAddress`와 같습니다.
- **비행 컨트롤러에는 일부러 연결하지 않습니다.** 방송을 FC를 거쳐 보내면 FC가 이상할 때 스피커도
  같이 죽는데, 경고방송은 바로 그런 상황에 가장 필요합니다. 스피커는 카메라와 같은 이더넷 세그먼트에
  독립 노드로 붙습니다.

## 1. OS 준비

Raspberry Pi OS Lite(64-bit) 설치 후:

```sh
sudo apt update
sudo apt install -y python3 mpg123 alsa-utils
```

## 2. 스피커 인식 확인

```sh
aplay -l            # "card N: ... [USB Audio ...]" 줄이 보여야 함
speaker-test -D plughw:N,0 -c 1 -t wav -l 1   # N은 위 카드 번호. 소리 나면 OK
```

데몬은 `aplay -l`에서 **USB**가 들어간 카드를 자동으로 고릅니다. 보드가 다른 카드를 잡으면
`speaker.service`의 `SPEAKER_ALSA_DEVICE=plughw:N,0`으로 고정하세요.

## 3. 고정 IP (에어유닛 망)

USB 이더넷 어댑터 인터페이스 이름 확인(`ip link` — 보통 `eth0` 또는 `enx…`), 그 인터페이스에:

```sh
sudo nmcli con add type ethernet ifname eth0 con-name air ipv4.method manual \
     ipv4.addresses 192.168.144.70/24 ipv4.gateway 192.168.144.12
sudo nmcli con up air
```

(구형 이미지의 dhcpcd는 `/etc/dhcpcd.conf`에 `interface eth0` / `static ip_address=192.168.144.70/24` /
`static routers=192.168.144.12`.)

확인: `ip addr show eth0`에 `192.168.144.70`이 보이고, 조종기 쪽에서 `ping 192.168.144.70`이 되면 됩니다.

## 4. 경고 음성 파일

```sh
sudo mkdir -p /opt/speaker/audio
sudo cp 01_해산안내.mp3 02_위험경고.mp3 03_접근금지.mp3 04_경찰안내.mp3 /opt/speaker/audio/
```

**정렬 순서 = 트랙 번호**이므로 파일명 앞에 `01_`, `02_`…를 붙이세요. 지원 포맷: mp3 / wav / ogg / flac.

위 4개 순서는 GCS의 기본 방송 문구(`해산 안내, 위험 경고, 접근 금지, 경찰 안내`)와 같습니다.
문구를 바꾸려면 GCS **설정 → 영상 → 스피커 페이로드 → 방송 문구 이름**에 쉼표로, **파일과 같은 순서**로
적습니다. 파일 수와 문구 수가 다르면 GCS 방송 패널에 경고가 뜹니다.
**스피커 기능은 GCS 설정에서 기본 꺼짐**이라 같은 화면에서 켜야 방송 버튼이 보입니다.

## 5. 데몬 설치

```sh
sudo cp speaker_daemon.py /usr/local/bin/
sudo cp speaker.service /etc/systemd/system/
sudo systemctl enable --now speaker
journalctl -u speaker -f      # 로그: "audio device: plughw:…", "control on udp/37270", "live audio on udp/37271"
```

## 6. 책상 테스트 (GCS 없이)

같은 망의 PC에서 `speaker_test_send.py`로:

```sh
python speaker_test_send.py --host 192.168.144.70 state       # playing/track/volume/tracks
python speaker_test_send.py --host 192.168.144.70 play 1      # 1번 파일 재생
python speaker_test_send.py --host 192.168.144.70 volume 70
python speaker_test_send.py --host 192.168.144.70 stop
# 라이브 마이크와 똑같이 WAV를 실시간으로 스트리밍 (16kHz/16bit/mono)
ffmpeg -i 아무거나.mp3 -ar 16000 -ac 1 -sample_fmt s16 test.wav
python speaker_test_send.py --host 192.168.144.70 mic test.wav
```

`mic` 스트리밍 중에 `play`가 재생 중이었다면 끊기고 라이브가 나와야 합니다(라이브 우선).
스트림이 끝나면 0.4초 뒤 자동으로 닫힙니다.

## 프로토콜 (GCS `SpeakerController`와 동일)

| 포트 | 내용 |
|---|---|
| UDP 37270 | 제어 — SIYI 프레이밍 `55 66 …` + CRC16/XMODEM. `0x01` PLAY(트랙) · `0x02` STOP · `0x03` VOLUME(%) · `0x04` STATE |
| UDP 37271 | 라이브 오디오 — `55 67` + seq(2, LE) + PCM(16kHz/16bit/mono LE), 20ms(640B)씩 |

카메라와 같은 프레이밍이라 기체가 제어 포맷 하나만 씁니다. **데몬은 모든 명령에 상태
`[playing, track, volume, trackCount]` 4바이트로 응답**하므로 별도 ACK 없이 결과를 알 수 있습니다.

## 데드맨

지상국 소식이 `SPEAKER_DEADMAN_SECONDS`(기본 120초) 동안 없으면 **저장 파일 재생**을 멈춥니다.
교신이 끊긴 채 날아간 기체가 계속 방송하는 걸 막기 위해서입니다. GCS가 1초마다 STATE를 폴링하므로
실제 교신 두절일 때만 만료됩니다. 라이브 마이크는 PTT를 놓으면 패킷이 끊겨 0.4초 뒤 스스로 닫히므로
데드맨과 무관합니다.
