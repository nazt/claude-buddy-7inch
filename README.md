# Claude Buddy 7"

ESP32-S3 + 7" 800x480 RGB touch display showing live Claude Code activity from a Linux box over WiFi.

Inspired by [anthropics/claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy) (M5StickC Plus + BLE) — ported to a bigger screen, WiFi/WebSocket transport, real Claude Code session log bridge.

## What it shows

- **HOME** — big animated ASCII pet (mood reflects activity), session counts, token total, latest tool/message, recent transcript lines, footer with WiFi/server status
- **PET** — pet name, approvals/denials, tokens
- **INFO** — connection details, stats, credits

Swipe left/right anywhere on screen to switch pages.

## Hardware

- Waveshare ESP32-S3-Touch-LCD-7.0
- ESP32-S3-WROOM-1, 8MB PSRAM, 16MB flash
- 800x480 RGB parallel display (ST7262)
- GT911 capacitive touch
- CH422G I2C IO expander (backlight, LCD reset, touch reset)

## Architecture

```
[Claude Code] → ~/.claude/projects/<path>/<sessionId>.jsonl
       │
       ▼
[claude_bridge.py] (Python, white.local:8765)
   - polls JSONL files every 2s
   - aggregates: total sessions, running (mtime < 30s), tokens, recent text
   - serves WebSocket to ESP32
       │  WS heartbeat snapshot every 2s
       ▼
[ESP32-S3 buddy] (192.168.1.x via WiFi)
   - Arduino_GFX RGB display
   - GT911 touch
   - state-hash gating (no flicker)
   - swipe to switch pages
   - tap APPROVE/DENY on prompt overlay
```

## Build & flash

```bash
cd /home/nat/8tb-oracle/claude-buddy-7inch
pio run                          # build
pio run -t upload                # flash via USB
# OR direct esptool:
PORT=$(find /dev -name "ttyACM*" | head -1)
esptool --chip esp32s3 --port $PORT --baud 921600 write_flash -z \
  0x0 .pio/build/esp32s3-lcd7/bootloader.bin \
  0x8000 .pio/build/esp32s3-lcd7/partitions.bin \
  0x10000 .pio/build/esp32s3-lcd7/firmware.bin
```

## Run server

### Manual

```bash
cd /home/nat/8tb-oracle/claude-buddy-7inch/server
uvx --with websockets python3 -u claude_bridge.py
```

### As systemd-user service (persistent across logins)

```bash
systemctl --user daemon-reload
systemctl --user enable --now claude-buddy
sudo loginctl enable-linger nat   # keep running after logout
```

Status: `systemctl --user status claude-buddy`
Logs: `journalctl --user -u claude-buddy -f`

## Wire protocol

Same as official [`anthropics/claude-desktop-buddy`](https://github.com/anthropics/claude-desktop-buddy/blob/main/REFERENCE.md), minus BLE-specific bits.

**Server → device** (heartbeat every 2s):
```json
{"total":498,"running":1,"waiting":0,"msg":"server [tool_use]",
 "entries":["claude-buddy: build OK","server: tool_use"],
 "tokens":1342,"tokens_today":1342,
 "owner":"Nat","pet":"claude"}
```

**Server → device** (on connect):
```json
{"cmd":"owner","name":"Nat"}
{"cmd":"name","name":"claude"}
```

**Device → server** (when user taps APPROVE/DENY):
```json
{"cmd":"permission","id":"<promptId>","decision":"once"|"deny"}
```

## Touch

- **Swipe left** = next page
- **Swipe right** = prev page
- **Tap APPROVE** = approve prompt (when displayed)
- **Tap DENY** = deny prompt

## Files

- `src/main.cpp` — display, touch, page state machine
- `src/wifi_link.cpp/.h` — WiFi + WebSocket client
- `src/protocol.h` — JSON heartbeat parser
- `src/touch_gt911.h` — touch driver
- `src/ch422g.h` — IO expander init
- `src/persist.h` — NVS storage for stats
- `server/claude_bridge.py` — real Claude Code session bridge (production)
- `server/server.py` — mock demo server
- `server/serial_server.py` — USB serial fallback (debug)

## Limitations

- Approval prompts (`waiting` count) currently always 0 — Claude Code doesn't log "permission pending" state in JSONL files. To detect: hook into Claude Code's permission flow via stdin/stdout or wrapper.
- APPROVE/DENY taps go to bridge but bridge has no way to inject decision back to Claude Code yet — would need Claude Code IPC integration.
- Used as live monitor / mood display, not interactive controller (until permission integration added).

## Configuration

Hardcoded in `src/wifi_link.h`:
```c
#define WIFI_SSID "Laris-co2"
#define WIFI_PASS "@O53732i36"
#define WS_HOST   "192.168.1.164"   // white.local on LAN
#define WS_PORT   8765
```

Change + rebuild for different network/server.

## Demo mode

When server unreachable, device falls back to local demo cycle (Idle → Busy → Attention → Happy → Sleep) so display stays alive even if bridge is down. Status strip shows "DEMO MODE" in red.
