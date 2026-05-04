# Claude Desktop Buddy - Complete Architecture Documentation

**Date**: 2026-05-04  
**Device**: M5StickC Plus (ESP32 + 135×240 TFT + AXP192 PMU + IMU)  
**Firmware**: Arduino framework via PlatformIO  
**Role**: BLE-connected desktop pet that monitors Claude sessions

---

## Table of Contents

1. [Directory Structure](#directory-structure)
2. [Entry Points & Main Loop](#entry-points--main-loop)
3. [Module Relationships](#module-relationships)
4. [Build Configuration](#build-configuration)
5. [File System Layout](#file-system-layout)
6. [Resource Budget](#resource-budget)

---

## Directory Structure

```
/tmp/claude-desktop-buddy/
├── platformio.ini                 # PlatformIO build config
├── README.md                      # User-facing guide
├── REFERENCE.md                   # Wire protocol spec
├── CONTRIBUTING.md                # Contribution guidelines
├── LICENSE                        # Open source license
│
├── src/
│   ├── main.cpp                   # Main loop, UI, state machine (1266 lines)
│   ├── buddy.cpp                  # ASCII species dispatcher + render helpers
│   ├── buddy.h                    # Buddy interface (multi-species system)
│   ├── buddy_common.h             # Shared geometry, colors, render helpers
│   ├── character.cpp              # GIF character loader & animator
│   ├── character.h                # GIF interface (palette, states, rendering)
│   ├── ble_bridge.cpp             # BLE stack: Nordic UART Service
│   ├── ble_bridge.h               # BLE interface (init, connect, R/W, passkey)
│   ├── data.h                     # Wire protocol parser + state dispatch
│   ├── xfer.h                     # File push receiver (character install)
│   ├── stats.h                    # NVS-backed stats, settings, names
│   │
│   └── buddies/                   # 18 ASCII species (one file each, ~120 lines/file)
│       ├── capybara.cpp           # Capybara (5 poses × 7 states)
│       ├── duck.cpp               # Duck
│       ├── blob.cpp               # Blob
│       ├── cat.cpp                # Cat
│       ├── dragon.cpp             # Dragon
│       ├── octopus.cpp            # Octopus
│       ├── owl.cpp                # Owl
│       ├── penguin.cpp            # Penguin
│       ├── turtle.cpp             # Turtle
│       ├── snail.cpp              # Snail
│       ├── ghost.cpp              # Ghost
│       ├── axolotl.cpp            # Axolotl
│       ├── cactus.cpp             # Cactus
│       ├── robot.cpp              # Robot
│       ├── rabbit.cpp             # Rabbit
│       ├── mushroom.cpp           # Mushroom
│       ├── chonk.cpp              # Chonk (chubby cat)
│       └── goose.cpp              # Goose
│
├── tools/
│   ├── prep_character.py          # GIF prep: downscale & crop to 96px
│   ├── flash_character.py         # USB-direct character flash (avoids BLE)
│   ├── test_serial.py             # Serial protocol debugging
│   └── test_xfer.py               # Character transfer testing
│
├── characters/
│   └── bufo/                      # Example GIF character pack
│       ├── manifest.json          # Metadata + state→filename mapping
│       ├── idle_0.gif             # Idle animation frame set
│       ├── idle_1.gif
│       ├── idle_2.gif
│       ├── ... (sleep, busy, attention, celebrate, dizzy, heart GIFs)
│       └── README.md              # Character pack info
│
└── docs/
    ├── device.jpg                 # Hardware photo
    ├── hardware-buddy-window.png  # Desktop app screenshot
    ├── menu.png                   # Menu screenshot
    └── manual.html                # User manual
```

---

## Entry Points & Main Loop

### `main.cpp` - The Control Center

**Setup Phase** (`setup()`, lines 938–986):
- Initializes M5Stack hardware (LCD, IMU, beeper, LED)
- Starts Bluetooth with device name "Claude-XXXX" (last 2 BT MAC bytes)
- Mounts LittleFS and loads character (GIF) or falls back to ASCII
- Loads stats, settings, pet name from NVS (Preferences)
- Renders splash screen with owner's name or "Hello!"
- Prepares TFT sprite: 135×240 pixels

**Main Loop** (`loop()`, lines 988–1265):

The loop runs at ~60fps (16ms tick):

```
[EVERY FRAME]:
  1. dataPoll(&tama)              # Parse JSON from USB/BT, update state
  2. statsPollLevelUp()           # Check for level-up celebration
  3. derive(tama)                 # Determine base mood from sessions/waiting
  4. Apply active state (oneshot  # Dizzy, heart, celebrate override baseState
     or baseState)
  5. LED pulse if attention       # Notification blink
  6. Shake detection → dizzy      # IMU accel delta check
  7. Button handling              # Menu, approval, screen, display mode
  8. Power button (AXP)           # Screen on/off, power management
  9. BtnA long-press (600ms)      # Menu open/close
  10. BtnA short-press            # Approve prompt / cycle display / menu
  11. BtnB short-press            # Deny prompt / next page / scroll
  12. Charging clock render       # RTC display on USB power
  13. Orientation detection       # Portrait/landscape based on IMU
  14. Buddy tick (ASCII) or       # Animate chosen species or GIF
      character tick (GIF)
  15. UI overlay draw             # HUD, menu, pet stats, info pages
  16. Face-down nap detection     # Energy recovery via gravity detection
  17. Screen auto-off (30s idle)  # Save power
  18. 16ms or 100ms delay         # Sleep if screenOff
```

**Key State Variables**:
- `TamaState tama` — Live session data from bridge (main.cpp:35)
- `PersonaState baseState, activeState` — Mood: sleep/idle/busy/attention/celebrate/dizzy/heart
- `DisplayMode displayMode` — DISP_NORMAL (home) / DISP_PET (stats) / DISP_INFO (help)
- `bool buddyMode` — ASCII (true) vs GIF (false)
- `uint8_t clockOrient` — Portrait (0) / Landscape BtnA-side (1) / USB-side (3)

---

## Module Relationships

### Communication Flow

```
┌─────────────────────────────────────────────────────────────┐
│                    MAIN LOOP (main.cpp)                     │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐  │
│  │ 1. dataPoll() calls ble_bridge + xfer               │  │
│  └────────────┬─────────────────────────────────────────┘  │
│               │                                             │
│      ┌────────▼────────┬──────────────┐                     │
│      │                 │              │                     │
│  ┌───▼────┐    ┌──────▼───┐  ┌──────▼────┐                │
│  │ BLE RX │    │ USB RX   │  │ xfer.h    │                │
│  │ (line  │    │ (line    │  │ (file     │                │
│  │ buff)  │    │ buff)    │  │ push rx)  │                │
│  └───┬────┘    └────┬─────┘  └──────┬────┘                │
│      │             │                │                      │
│      └─────────┬───┴────────────────┘                      │
│                │                                            │
│         ┌──────▼──────────┐                                │
│         │ data.h:         │                                │
│         │ _applyJson()    │                                │
│         │ xferCommand()   │                                │
│         └──────┬──────────┘                                │
│                │                                            │
│         ┌──────▼──────────────────────────────────────┐    │
│         │ Update tama (sessions, waiting, tokens      │    │
│         │ Update character (name, species via xfer)  │    │
│         │ Update stats (token delta → level up)       │    │
│         │ Update RTC (time sync from bridge)          │    │
│         └──────┬──────────────────────────────────────┘    │
│                │                                            │
│      ┌─────────▼──────────┬──────────────┬────────────┐   │
│      │                    │              │            │   │
│  ┌───▼──────┐  ┌────────▼─┴──┐  ┌──────▼──┐  ┌──────▼──┐ │
│  │ stats.h  │  │ character.h │  │buddy.h  │  │ other   │ │
│  │ (NVS      │  │ (GIF load,  │  │(ASCII    │  │ state   │ │
│  │ persist)  │  │ animate)    │  │animate)  │  │updates  │ │
│  └──────────┘  └─────────────┘  └────────┘  └────────┘ │
│                                                          │
│  ┌──────────────────────────────────────────────────────┐ │
│  │ 2. Render phase                                      │ │
│  │    - buddyTick(activeState)  OR                      │ │
│  │    - characterTick() + setstate                      │ │
│  │    - drawHUD/drawInfo/drawPet overlays              │ │
│  │    - spr.pushSprite(0, 0) to LCD                   │ │
│  └────────────┬─────────────────────────────────────────┘ │
│               │                                            │
│      ┌────────▼───────────┐                               │
│      │ TFT_eSprite spr    │                               │
│      │ (135×240 buffer)   │                               │
│      └────────┬───────────┘                               │
│               │                                            │
│      ┌────────▼──────────┐                                │
│      │ M5 LCD driver     │                                │
│      │ (SPI to screen)   │                                │
│      └───────────────────┘                                │
│                                                           │
│  ┌──────────────────────────────────────────────────────┐ │
│  │ 3. Send responses back to bridge                    │ │
│  │    sendCmd() → bleWrite() + Serial.println()       │ │
│  └──────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

### Module-to-Module Dependencies

```
main.cpp
  ├─ #include "ble_bridge.h"      → bleInit(), bleWrite(), blePasskey()
  ├─ #include "data.h"            → dataPoll() and wire protocol
  ├─ #include "character.h"       → GIF loading, rendering, state
  ├─ #include "buddy.h"           → ASCII species management
  ├─ #include "stats.h"           → NVS persistence (stats, settings, names)
  └─ M5StickCPlus.h               → Hardware drivers (LCD, IMU, Beep, AXP)

data.h
  ├─ #include "ble_bridge.h"      → bleAvailable(), bleRead(), bleWrite()
  ├─ #include "xfer.h"            → xferCommand() (character install)
  └─ ArduinoJson.h                → JSON parsing

xfer.h
  ├─ #include "ble_bridge.h"      → bleWrite() for acks
  ├─ #include "character.h"       → characterInit(), characterClose()
  ├─ #include "stats.h"           → Preferences NVS access
  └─ mbedtls/base64.h + LittleFS  → Base64 decode, filesystem write

character.h
  ├─ M5StickCPlus.h               → TFT_eSPI sprite, display
  ├─ AnimatedGIF.h                → GIF decoding library
  ├─ LittleFS.h                   → Character pack file access
  └─ ArduinoJson.h                → manifest.json parsing

buddy.h + buddy_common.h
  ├─ Each buddies/*.cpp           → Species implementations
  ├─ M5StickCPlus.h               → TFT_eSPI sprite, text rendering
  └─ buddy_common.h               → Shared colors, geometry, helpers

ble_bridge.cpp
  ├─ BLEDevice.h, BLEServer.h     → ESP32 native BLE stack
  ├─ BLE2902.h                    → CCCD (client characteristic config)
  └─ Preferences.h (implicit)     → Bond storage (NVS)

stats.h
  └─ Preferences.h                → NVS key-value store

buddy.cpp
  └─ buddies/capybara.cpp etc.    → Species registry, state dispatch
```

### Data Flow: Approval Prompt Example

```
[DESKTOP APP sends JSON over BLE]
  {"prompt":{"id":"UUID","tool":"web_search","hint":"find latest news"}}
        ↓
[ble_bridge.cpp: RxCallbacks::onWrite]
  rxPush() → ring buffer (rxBuf)
        ↓
[main.cpp loop: dataPoll()]
  data.h: _LineBuf::feed(BLE) → _applyJson()
        ↓
[data.h: _applyJson()]
  doc["prompt"] parsed → tama.promptId, promptTool, promptHint set
  lastPromptId changed → responseSent = false, wake()
        ↓
[main.cpp loop: button handling]
  M5.BtnA.wasReleased() (without long-press)
    if (inPrompt) {
      snprintf(cmd, "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"once\"}")
      sendCmd() → bleWrite() → desktop app
      responseSent = true, beep, trigger heart
    }
        ↓
[DESKTOP APP receives approval command]
  Executes the allowed action
```

---

## Build Configuration

### `platformio.ini`

```ini
[env:m5stickc-plus]
platform = espressif32                    # ESP32 board support
board = m5stick-c                         # Board variant
framework = arduino                       # Arduino-compatible API

monitor_speed = 115200                    # Serial debug output

# Filesystem
board_build.filesystem = littlefs         # LittleFS for character storage
board_build.partitions = no_ota.csv       # No OTA updates needed
board_build.f_cpu = 160000000L            # Clock speed

# Build flags
build_flags =
    -DCORE_DEBUG_LEVEL=0                  # Suppress verbose logs
    
build_src_filter = +<*> +<buddies/>       # Include all src/ and buddies/

# Dependencies
lib_deps =
    m5stack/M5StickCPlus                  # Hardware drivers
    bitbank2/AnimatedGIF @ ^2.1.1         # GIF decoding
    bblanchon/ArduinoJson @ ^7.0.0        # JSON parsing
```

**Memory Partitioning** (no_ota.csv):
- **Bootloader**: 8KB
- **Partition table**: 4KB
- **App (SPIFFS/LittleFS)**: ~1.8 MB for character storage
- **PSRAM**: 2MB available (not heavily used; mostly for GIF framebuffer)

**Compile Optimizations**:
- `CORE_DEBUG_LEVEL=0` suppresses serial.printf() overhead
- Arduino framework: lightweight vs full IDF

---

## File System Layout

### LittleFS Mount Point: `/`

**Root-level files** (via Preferences, NVS):
- None on filesystem; all config in Preferences namespace "buddy"

**Directory Structure**:
```
/characters/
  └── <name>/                           # One character pack at a time
      ├── manifest.json                 # Colors, state→GIF map
      ├── sleep.gif                     # Or array: [idle_0.gif, idle_1.gif, ...]
      ├── idle_0.gif
      ├── idle_1.gif
      ├── idle_2.gif
      ├── busy.gif
      ├── attention.gif
      ├── celebrate.gif
      ├── dizzy.gif
      └── heart.gif
```

### NVS Namespace: `"buddy"`

**Stats** (persistent, written on approval/denial/nap-end):
- `"nap"` → `uint32_t` napSeconds (cumulative face-down nap time)
- `"appr"` → `uint16_t` approvals (count)
- `"deny"` → `uint16_t` denials (count)
- `"vidx"` → `uint8_t` velocity ring buffer index
- `"vcnt"` → `uint8_t` velocity ring buffer count
- `"lvl"` → `uint8_t` level (derived from tokens)
- `"tok"` → `uint32_t` cumulative tokens
- `"vel"` → `uint8_t[8]` seconds-to-respond ring buffer (4–5 second response speeds)

**Settings** (user preferences):
- `"s_snd"` → `bool` sound enabled
- `"s_bt"` → `bool` Bluetooth advertise preference (stored; stack always on)
- `"s_wifi"` → `bool` WiFi placeholder (no WiFi stack linked)
- `"s_led"` → `bool` LED pulse on attention
- `"s_hud"` → `bool` transcript HUD on home screen
- `"s_crot"` → `uint8_t` clock orientation lock (0=auto, 1=portrait, 2=landscape)

**Owner & Pet Identity**:
- `"petname"` → `char[24]` pet's name (default "Buddy")
- `"owner"` → `char[32]` owner's name (empty until pushed by desktop)

**Species Choice**:
- `"species"` → `uint8_t` (0..17 = ASCII species index, 0xFF = use GIF)

**BLE Bonding** (managed by ESP32 stack):
- Stored by the BLE security layer in NVS under a separate namespace; not directly accessible

**Character Transfer** (xfer.h):
- Files written directly to `/characters/<name>/` by file descriptors
- No intermediate temp; LittleFS handles atomic writes

---

## Resource Budget

### PSRAM Usage

**Total Available**: 2MB (M5StickC Plus)

**Allocation**:
- **TFT sprite** (main.cpp:954): 135 × 240 × 2 bytes (RGB565) = **64.8 KB**
- **GIF framebuffer** (character.cpp): ~96 × 140 × 3 bytes (RGB, before conversion) = **40 KB** (transient, per-frame decode)
- **JSON document** (data.h): ~512 bytes (re-used per parse)
- **Line buffers** (data.h): `_LineBuf<1024>` USB + BLE = **2 KB** total
- **Transcript** (data.h): 8 lines × 92 chars = **736 bytes**
- **Stack & heap overhead**: Remaining ~1.8 MB

**Critical**: Sprite must be in-memory throughout loop; GIF is decoded per-frame.

### Sprite Usage (135×240, 2 bytes/pixel)

**Layout** (portrait):
```
[  0.. 69] — Character (GIF/ASCII, full width)
[ 70.. 99] — Pet stats header + bars OR info section
[100..239] — HUD (transcript, approval, or extended content)
```

**Peek Mode** (GIF at half-scale in 70px info header):
- Scales to 48×70 max
- Centered in the info panel

**Landscape Clock** (240×135, direct-to-LCD rotation):
- Pet on left: 115×90 (ASCII only, no GIF in landscape)
- Clock on right: time, date, seconds

### GIF Cache Constraints

**Per-State Files**:
- 96px wide × ~100–140px tall (variable per character)
- Typically **5–20 KB per GIF** (lossy 64-color palette + compression)

**Total Character Pack**:
- 7 states × ~10 KB avg = **70 KB**
- Example bufo pack: ~85 KB across 17 GIFs
- **Limit**: ~1.8 MB LittleFS, shared with firmware + margin = expect **300–400 KB max per character**

**Frame Decoding**:
- One frame decoded into RGB buffer (~40 KB transient)
- Scaled to sprite (135×240)
- Next frame on next tick (~16ms @ 60fps)

### Transcript Buffer

**Hard limit**: 8 lines × 92 chars (main.cpp:16, TamaState.lines)

**Word-wrap display** (main.cpp:693–723):
- `wrapInto()` re-wraps transcript into display rows (21 chars wide)
- 3 visible rows on 135px screen (8pt font at 6px width)
- Scroll buffer up to 32 display rows from 8 transcript rows

**Message HUD** (main.cpp:890–936):
- Shows 3 latest wrapped lines
- Can scroll back to see earlier
- Button B cycles scroll offset

### CPU Budget

**Loop iteration**: ~16ms @ 60fps

**Per-tick overhead**:
- `dataPoll()` (USB/BLE parse): **~1–5ms** (depends on JSON size)
- `characterTick()` (GIF decode): **~5–10ms** (varies by frame complexity)
- `buddyTick()` (ASCII render): **~1–3ms** (simple text ops)
- Button & sensor reads: **<1ms**
- `spr.pushSprite()` (SPI to LCD): **~5ms** at 160MHz
- Remaining: margin for NVS, GC, etc.

**Debouncing & hysteresis**:
- Face-down nap: 20 frames to enter, 10 frames to exit
- Orientation swing: 8–15 frames hysteresis
- Shake detection: baseline low-pass filtered

---

## Key Implementation Details

### ASCII Buddy System (buddy.h + buddies/*.cpp)

Each species is a `.cpp` file with 7 state functions:

```cpp
struct Species {
  const char* name;                    // e.g., "capybara"
  uint16_t bodyColor;                  // RGB565 color for this species
  StateFn states[7];                   // [sleep, idle, busy, attention, celebrate, dizzy, heart]
};

// Per state: animated render using text (space-padded lines)
typedef void (*StateFn)(uint32_t t);   // t = global tick counter
```

**Example: Capybara sleep** (src/buddies/capybara.cpp:11–43):
- 6 poses (FLAT, BREATHE, SNORE, SIDE, SIDE_Z, YAWN)
- Animated via 24-frame sequence (beat = tick / 5 % len)
- Particle Z's drift up-right via buddySetCursor/buddyPrint overlay

**Rendering**:
- `buddyPrintSprite()` centers text at BUDDY_X_CENTER (67)
- Scale applied: 1× on landscape/peek, 2× on home screen
- Sprite-local Y offset applied per line

### Character (GIF) System (character.h + character.cpp)

**Manifest Format**:
```json
{
  "name": "bufo",
  "colors": {
    "body": "#6B8E23",
    "bg": "#000000",
    "text": "#FFFFFF",
    "textDim": "#808080",
    "ink": "#000000"
  },
  "states": {
    "sleep": "sleep.gif",
    "idle": ["idle_0.gif", "idle_1.gif", "idle_2.gif"],
    "busy": "busy.gif",
    "attention": "attention.gif",
    "celebrate": "celebrate.gif",
    "dizzy": "dizzy.gif",
    "heart": "heart.gif"
  }
}
```

**Installation** (xfer.h: char_begin → file → chunk → file_end → char_end):
1. `char_begin {"name":"bufo","total":150000}` — validate space, wipe old
2. `file {"path":"manifest.json","size":500}` — open file
3. `chunk {"d":"<base64>"}` → `chunk {"d":"..."}` — stream chunks
4. `file_end` — close, verify size
5. Repeat for each GIF
6. `char_end` — load & initialize

**Rendering** (AnimatedGIF library):
- Callbacks: gifOpenCb, gifReadCb, gifSeekCb, gifCloseCb
- Per-frame decode into RGB buffer
- Scale & center on sprite
- Next frame triggered by tick (nextFrameAt)
- Transparent pixels → character bg color (no ghosting)

### State Machine (derive + oneShotUntil)

**Base State** (main.cpp:479–485, derive):
```cpp
PersonaState derive(const TamaState& s) {
  if (!s.connected)            return P_IDLE;        // no bridge
  if (s.sessionsWaiting > 0)   return P_ATTENTION;   // urgent!
  if (s.recentlyCompleted)     return P_CELEBRATE;   // just finished
  if (s.sessionsRunning >= 3)  return P_BUSY;        // multitasking
  return P_IDLE;                                      // idle, connected
}
```

**One-shot overrides** (main.cpp:1002):
- `triggerOneShot(PersonaState s, uint32_t durMs)` sets activeState for duration
- Examples: dizzy on shake (2s), heart on fast approval (<5s)
- Expires via: `if ((int32_t)(now - oneShotUntil) >= 0) activeState = baseState`

**Wake transition** (main.cpp:1000):
- On screen wake, hold P_SLEEP for 12s so user sees animation
- Overridden by urgent states (attention, busy, celebrate)

### Level-Up & Token Tracking (stats.h)

**Token Source**: Bridge sends cumulative tokens since its startup.

**Level Derivation** (stats.h:64–109):
```cpp
#define TOKENS_PER_LEVEL 50000

// Bridge restart detection: drop in cumulative → resync
if (bridgeTotal < _lastBridgeTokens) {
  _lastBridgeTokens = bridgeTotal;  // bridge restarted
  return;
}

// Accumulate delta
uint32_t delta = bridgeTotal - _lastBridgeTokens;
uint8_t lvlBefore = _stats.tokens / TOKENS_PER_LEVEL;
_stats.tokens += delta;
uint8_t lvlAfter = _stats.tokens / TOKENS_PER_LEVEL;

if (lvlAfter > lvlBefore) {
  _levelUpPending = true;             // triggers celebration on next poll
  _dirty = true; statsSave();          // persist new level to NVS
}
```

**Fed Progress**: 10 pips per level (tokens % 50000) / 5000

**Mood Tier** (stats.h:142–158):
- Base: median of last 8 approval response times (0–4 tier)
- Penalty: deny ratio >33% → -1 tier, >50% → -2 tiers

### BLE Bridge (ble_bridge.cpp)

**Service**: Nordic UART (NUS) — standard UUIDs, widely supported

```
Service UUID:    6e400001-b5a3-f393-e0a9-e50e24dcca9e
RX (client write): 6e400002-b5a3-f393-e0a9-e50e24dcca9e  (write, no response)
TX (notify):      6e400003-b5a3-f393-e0a9-e50e24dcca9e  (NOTIFY)
```

**Connection**:
- `bleInit(deviceName)` → advertise as "Claude-XXXX"
- LE Secure Connections with passkey entry
- Desktop reads 6-digit passkey from screen and types it
- Bond persists; auto-reconnect on next startup

**RX Pipeline** (ble_bridge.cpp:33–46):
- Incoming GATT writes → RxCallbacks::onWrite()
- Bytes pushed to ring buffer (rxBuf, 2048 cap)
- Main loop polls: while (bleAvailable()) → bleRead()
- Lines buffered in data.h until `\n`, then parsed

**TX Pipeline** (ble_bridge.cpp: bleWrite):
- Data chunked to negotiated MTU (often 185 on macOS)
- Characteristic notifies client

**MTU Negotiation** (ble_bridge.cpp:63–66):
- Request 517; macOS negotiates to ~185
- Base MTU always 23 (Bluetooth spec minimum)

---

## Example Flows

### Boot-to-Idle

```
setup() {
  M5.begin()                           # Init hardware
  startBt()                            # bleInit("Claude-XXXX")
  statsLoad()                          # NVS → _stats
  settingsLoad()                       # NVS → _settings
  petNameLoad()                        # NVS → _petName, _ownerName
  buddyInit()                          # Register 18 ASCII species
  characterInit(nullptr)               # Scan /characters/ → gifAvailable
  
  # Load saved species choice
  buddyMode = !(gifAvailable && speciesIdxLoad() == 0xFF)
  
  # Splash screen
  renderSplash()
  delay(1800)
}

loop() {
  dataPoll(&tama)                      # Sees no bridge → tama.connected = false
  baseState = derive(tama)             # P_IDLE → "No Claude connected"
  
  # Screen stays on, buddy sleeps
  characterSetState(P_SLEEP)
  characterTick()                      # Animate sleeping GIF
  drawHUD()                            # "No Claude connected"
  spr.pushSprite(0, 0)                 # LCD update
}
```

### Approval Prompt to Execution

```
[Desktop sends over BLE]
{"prompt":{"id":"req-123","tool":"web_search","hint":"latest news"}}

loop() {
  dataPoll()
    → ble_bridge RX → data._applyJson()
    → tama.promptId = "req-123", promptTool = "web_search"

  # Detect new prompt (promptId changed)
  if (strcmp(tama.promptId, lastPromptId) != 0) {
    strncpy(lastPromptId, tama.promptId, ...)
    responseSent = false
    if (tama.promptId[0]) {
      promptArrivedMs = millis()
      wake()                           # Power on screen
      beep(1200, 80)                   # Alert chirp
      displayMode = DISP_NORMAL        # Jump to home
      characterInvalidate()            # Force re-render
    }
  }

  # User presses BtnA
  if (M5.BtnA.wasReleased() && !btnALong && inPrompt) {
    snprintf(cmd, "{\"cmd\":\"permission\",\"id\":\"req-123\",\"decision\":\"once\"}")
    sendCmd(cmd)
      → Serial.println(cmd)
      → bleWrite(cmd, len) + bleWrite("\n", 1)
    
    responseSent = true
    tookS = (millis() - promptArrivedMs) / 1000
    statsOnApproval(tookS)             # _stats.approvals++, velocity ring, NVS save
    beep(2400, 60)                     # Success beep
    if (tookS < 5) triggerOneShot(P_HEART, 2000)
  }

  # Render loop
  if (buddyMode) buddyTick(P_HEART)   # 2s of hearts
  drawApproval()                       # "sent..." footer
  spr.pushSprite(0, 0)

  # After 2s, revert to base state
  if ((int32_t)(millis() - oneShotUntil) >= 0) activeState = baseState
}
```

### Character Installation (Drag-and-Drop)

```
[Desktop app: user drags folder onto Hardware Buddy window]
  Desktop opens char_begin → file → chunk → ... → file_end → char_end

[Device receives char_begin]
xferCommand():
  Validate total size against free space
  _xWipeAllChars()  # Clear /characters/<old>/*
  LittleFS.mkdir("/characters/bufo")
  _xActive = true, _xTotalWritten = 0

[For each file]
xferCommand("file"):
  LittleFS.open("/characters/bufo/manifest.json", "w") → _xFile

xferCommand("chunk"):
  mbedtls_base64_decode(doc["d"]) → raw bytes
  _xFile.write(raw, outLen)
  _xWritten += outLen
  ack with progress

xferCommand("file_end"):
  _xFile.close()
  Verify _xWritten == _xExpected
  ack

[After all files]
xferCommand("char_end"):
  characterInit("bufo")  # Parse manifest.json, load GIF paths
  buddyMode = false, gifAvailable = true, speciesIdxSave(0xFF)
  main.cpp picks up gifAvailable in next tick
  
  mainloop() {
    characterSetState(activeState)  # Opens bufo/idle.gif
    characterTick()                 # Decodes & renders
  }
```

### Nap & Energy Recovery

```
loop() {
  # Check acceleration every 50ms
  if (now - lastShakeCheck > 50) {
    lastShakeCheck = now
    
    # isFaceDown checks Z-axis
    az < -0.7f && |ax| < 0.4f && |ay| < 0.4f
    
    # Debounce with signed counter
    if (down)       faceDownFrames = min(20, faceDownFrames + 1)
    else            faceDownFrames = max(-10, faceDownFrames - 1)
    
    # Enter nap at +15
    if (!napping && faceDownFrames >= 15) {
      napping = true
      napStartMs = now
      M5.Axp.ScreenBreath(8)       # Dim to 8/255
      
      # Skip sprite render (no animation during nap)
    }
    
    # Exit nap at -8 (hysteresis ensures ~0.5s flipping doesn't bounce)
    if (napping && faceDownFrames <= -8) {
      napping = false
      napDurationS = (now - napStartMs) / 1000
      statsOnNapEnd(napDurationS)   # _stats.napSeconds += napDurationS, NVS save
      statsOnWake()                 # _energyAtNap = 5, _lastNapEndMs = now
      wake()                        # Restore brightness
    }
  }

  # Energy tier calculated on-the-fly (stats.h:166–171)
  uint8_t en = statsEnergyTier()
    // hoursSince = (millis - lastNapEndMs) / 3600000
    // e = _energyAtNap (5) - (hoursSince / 2)
    // Returns 0..5 (full after nap, drains 1 per 2 hours)
    
  # Draw energy bar as 5 pips, colored by tier
}
```

---

## Summary

**claude-desktop-buddy** is a tightly integrated firmware system for the M5StickC Plus that monitors Claude desktop sessions via BLE and renders visual feedback through ASCII sprites or GIF animations. The architecture cleanly separates concerns:

- **main.cpp**: Control logic, state machine, UI framework
- **ble_bridge.cpp**: BLE protocol layer (Nordic UART Service)
- **data.h**: Wire protocol parsing + JSON dispatch
- **xfer.h**: Character installation (file push receiver)
- **character.cpp** / **buddy.cpp**: Rendering engines (GIF + 18 ASCII species)
- **stats.h**: Persistent storage (NVS) for stats, settings, identity
- **buddies/*.cpp**: 18 species animation implementations

All assets fit in ~1.8 MB LittleFS, PSRAM is carefully managed for the sprite and GIF decode buffers, and the main loop sustains 60fps rendering while polling BLE, parsing JSON, and managing sensors—enabling a lively, responsive desk companion that truly responds to your work.

