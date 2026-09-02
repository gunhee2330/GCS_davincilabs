# Loudspeaker payload — Raspberry Pi side

The ground station sends a track number over UDP; this daemon plays the matching file
through the amplifier. Audio lives here, not on the ground station, so a broadcast already
under way survives a link drop.

## Wiring

    ZT30 ── [AI module .60] ── SIYI FPV Hub ── Air unit (.11)
                                    │
                                    └── Raspberry Pi (.70) ── amplifier ── horn speaker

The Pi joins the aircraft's existing Ethernet segment. It is deliberately **not** wired to
the flight controller: routing broadcasts through the autopilot would make the loudspeaker
depend on autopilot health, and a warning broadcast is most needed when things are going
wrong.

## Install

```bash
sudo mkdir -p /opt/speaker/audio
sudo cp speaker_daemon.py /usr/local/bin/
sudo cp speaker.service /etc/systemd/system/
sudo apt install -y mpg123
sudo systemctl enable --now speaker
```

Static address, matching what the ground station is configured with
(`/etc/dhcpcd.conf` or a NetworkManager profile):

```
interface eth0
static ip_address=192.168.144.70/24
static routers=192.168.144.12
```

## Audio files

Drop numbered files in `/opt/speaker/audio`. **Sort order is track order**, so prefix them:

```
/opt/speaker/audio/01-disperse.mp3     -> track 1
/opt/speaker/audio/02-danger.mp3       -> track 2
/opt/speaker/audio/03-no-entry.mp3     -> track 3
```

Then set the matching labels in the ground station under
Settings → Video → Loudspeaker Payload → Broadcast message names, comma separated and in
the same order. The ground station shows a warning when the counts disagree.

## Checking it works, without an aircraft

```bash
# On the Pi
sudo systemctl status speaker
journalctl -u speaker -f

# From any machine on the same network
ping 192.168.144.70
```

## Protocol

Same packet framing as the SIYI camera, so the airframe carries one wire format:

    0x55 0x66 | CTRL(1) | DATA_LEN(2, LE) | SEQ(2, LE) | CMD_ID(1) | DATA | CRC16(2, LE)

CRC-16/XMODEM, polynomial 0x1021, initial value 0.

| CMD | Direction | Payload |
|-----|-----------|---------|
| 0x01 Play | GCS → Pi | track number, 1 based |
| 0x02 Stop | GCS → Pi | none |
| 0x03 SetVolume | GCS → Pi | percent 0-100 |
| 0x04 RequestState | GCS → Pi | none |
| any | Pi → GCS | playing(1) track(1) volume(1) trackCount(1) |

The Pi answers every command with its state, so the ground station learns the result
without a separate acknowledgement.

## Deadman

If nothing is heard from the ground station for `SPEAKER_DEADMAN_SECONDS` (default 120),
playback stops. This bounds a stuck broadcast on an aircraft that has flown out of contact.
The ground station polls once a second, so the timer only expires on a real loss of contact.
