#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>

#include "ch422g.h"
#include "wifi_link.h"
#include "protocol.h"
#include "touch_gt911.h"
#include "persist.h"

// ───── Display ─────
Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
  5, 3, 46, 7,
  1, 2, 42, 41, 40,
  39, 0, 45, 48, 47, 21,
  14, 38, 18, 17, 10,
  0, 8, 4, 8,
  0, 8, 4, 8,
  1, 16000000
);
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(800, 480, bus, 0, true);

#define C_BG     0x0000
#define C_PANEL  0x18C3
#define C_TEXT   0xFFFF
#define C_DIM    0x8410
#define C_BODY   0xC2A6
#define C_GREEN  0x07E0
#define C_RED    0xF800
#define C_HOT    0xFA20
#define C_CYAN   0x07FF
#define C_DARKG  0x0400
#define C_DARKR  0x4000

const int W = 800, H = 480;

// ───── Touch zones ─────
struct Zone { int x, y, w, h; };
static const Zone Z_APPROVE  = { 40,  340, 340, 100 };
static const Zone Z_DENY     = { 420, 340, 340, 100 };
// Swipe thresholds
static const int SWIPE_MIN_DX = 80;
static const int SWIPE_MAX_DY = 60;

static bool inZone(int tx, int ty, const Zone& z) {
  return tx >= z.x && tx < z.x + z.w && ty >= z.y && ty < z.y + z.h;
}

// ───── State ─────
static ClaudeState state;
static bool responseSent = false;
static char lastPromptId[40] = "";
static uint32_t promptArrivedMs = 0;
static uint32_t lastTouchMs = 0;
static uint32_t lastDrawMs = 0;

enum DispMode { M_HOME, M_PET, M_INFO, M_COUNT };
static DispMode dispMode = M_HOME;

enum Persona { P_SLEEP, P_IDLE, P_BUSY, P_ATTN, P_HAPPY };

struct DemoStep { Persona p; const char* msg; uint8_t total, run, wait; };
static const DemoStep DEMO[] = {
  { P_IDLE,  "Idle",                1, 0, 0 },
  { P_BUSY,  "3 sessions running",  4, 3, 0 },
  { P_ATTN,  "Approval pending",    2, 1, 1 },
  { P_HAPPY, "Task completed",      1, 0, 0 },
  { P_SLEEP, "Asleep",              0, 0, 0 },
};
static const uint8_t DEMO_N = sizeof(DEMO) / sizeof(DEMO[0]);
static uint8_t demoIdx = 0;
static uint32_t demoNextMs = 0;

static Persona derive() {
  if (!state.connected) {
    // Device-side demo: cycle scenarios when no server linked
    return DEMO[demoIdx].p;
  }
  if (state.sessionsWaiting > 0)    return P_ATTN;
  if (state.recentlyCompleted)      return P_HAPPY;
  if (state.sessionsRunning >= 2)   return P_BUSY;
  return P_IDLE;
}

static void demoTick() {
  if (state.connected) return;
  uint32_t now = millis();
  if (now >= demoNextMs) {
    demoNextMs = now + 4000;
    demoIdx = (demoIdx + 1) % DEMO_N;
    const DemoStep& d = DEMO[demoIdx];
    strncpy(state.msg, d.msg, sizeof(state.msg) - 1);
    state.msg[sizeof(state.msg) - 1] = 0;
    state.sessionsTotal = d.total;
    state.sessionsRunning = d.run;
    state.sessionsWaiting = d.wait;
    state.recentlyCompleted = (d.p == P_HAPPY);
  }
}

// ───── Species art: 6 species × 5 personas × 2 animation frames ─────
// Each persona has 2 frames for breathing/blink animation cycling at 1 Hz
struct Frame { const char* lines[5]; };
struct Species {
  const char* name;
  uint16_t bodyCol;
  Frame frames[5][2];  // [persona][frame]
};

// Cat
static const Species SP_CAT = {"cat", C_BODY, {
  // SLEEP, IDLE, BUSY, ATTN, HAPPY
  {{"  /\\_/\\  ", " ( -.- ) ", "  > ^ < z", " /|   |\\Z", "(_|   |_)"},
   {"  /\\_/\\  ", " ( -.- ) ", "  > ^ <  ", " /|   |\\z", "(_|   |_)"}},
  {{"  /\\_/\\  ", " ( o.o ) ", "  > ^ <  ", " /|   |\\ ", "(_|   |_)"},
   {"  /\\_/\\  ", " ( -.o ) ", "  > ^ <  ", " /|   |\\ ", "(_|   |_)"}},
  {{"  /\\_/\\  ", " ( @.@ ) ", "  > ^ <  ", " /|>>>|\\ ", "(_|   |_)"},
   {"  /\\_/\\  ", " ( @.@ ) ", "  > ^ <  ", " /|<<<|\\ ", "(_|   |_)"}},
  {{"  /\\_/\\ !", " ( O.O )!", "  > ^ <  ", " /|   |\\ ", "(_|   |_)"},
   {" /\\_/\\  !", "( O.O ) !", " > ^ <   ", "/|   |\\  ", "_|   |_) "}},
  {{"* /\\_/\\ *", " \\(^.^)/ ", "  > ^ <  ", " /|   |\\ ", "(_|   |_)"},
   {"+ /\\_/\\ +", "  (^.^)  ", "  > ^ <  ", " /| ^ |\\ ", "(_|   |_)"}},
}};

// Owl
static const Species SP_OWL = {"owl", 0xC2A6, {
  {{"  ,___,  ", " ( -.- ) ", " ( /v\\ ) ", " (_| |_) ", "   ^ ^   "},
   {"  ,___,  ", " ( -.- ) ", " ( |v| ) ", " (_| |_) ", "   ^ ^   "}},
  {{"  ,___,  ", " ( o.o ) ", " ( /v\\ ) ", " (_| |_) ", "   ^ ^   "},
   {"  ,___,  ", " ( -.o ) ", " ( /v\\ ) ", " (_| |_) ", "   ^ ^   "}},
  {{"  ,___,  ", " ( @.@ ) ", " ( /v\\ ) ", " (_| |_) ", "  > ^ <  "},
   {"  ,___,  ", " ( @.@ ) ", " ( |v| ) ", " (_| |_) ", "  < ^ >  "}},
  {{"  ,!!,   ", " ( O.O ) ", " ( /v\\ ) ", " (_| |_) ", "   ^ ^   "},
   {"  ,!!,   ", " ( O.O ) ", " (-/v\\-) ", " (_| |_) ", "   ^ ^   "}},
  {{"  ,___,  ", " (\\_._/) ", " ( /v\\ ) ", " (_|*|_) ", "   ^ ^   "},
   {"  ,___,  ", " (^.^) ", " ( /v\\ ) ", " (_|*|_) ", "   ^ ^   "}},
}};

// Robot
static const Species SP_ROBOT = {"robot", C_CYAN, {
  {{" .-----. ", " | -.- | ", " |  =  | ", " |_|=|_| ", "   |_|   "},
   {" .-----. ", " | -.- | ", " |  -  | ", " |_|=|_| ", "   |_|   "}},
  {{" .-----. ", " | o.o | ", " |  =  | ", " |_|=|_| ", "   |_|   "},
   {" .-----. ", " | o.- | ", " |  =  | ", " |_|=|_| ", "   |_|   "}},
  {{" .-----. ", " | x.x | ", " |#####| ", " |_|=|_| ", "   |_|   "},
   {" .-----. ", " | x.x | ", " |/////| ", " |_|=|_| ", "   |_|   "}},
  {{" .--!--. ", " | O.O | ", " |  =  | ", " |_|=|_| ", "   |_|   "},
   {" .--!--. ", " | O.O | ", " | !=! | ", " |_|=|_| ", "   |_|   "}},
  {{" .--*--. ", " |\\^.^/| ", " |  =  | ", " |_|=|_| ", "   |_|   "},
   {" .--*--. ", " | ^.^ | ", " |  v  | ", " |_|=|_| ", "   |_|   "}},
}};

// Duck
static const Species SP_DUCK = {"duck", 0xFFE0, {
  {{"   __    ", " <(-.-)  ", "  ( v )  ", "   |_|   ", "   ^^    "},
   {"   __    ", " <(-.-)  ", "  ( v )  ", "   |_|   ", "    ^    "}},
  {{"   __    ", " <(o.o)  ", "  ( v )  ", "   |_|   ", "   ^^    "},
   {"   __    ", " <(-.o)  ", "  ( v )  ", "   |_|   ", "   ^^    "}},
  {{"   __    ", " <(@.@)  ", "  ( v )  ", "  /|_|\\  ", "   ^^    "},
   {"   __    ", " <(@.@)  ", "  ( v )  ", "  \\|_|/  ", "   ^^    "}},
  {{"   _!_   ", " <(O.O)  ", "  ( v )  ", "   |_|   ", "   ^^    "},
   {"   _!_   ", " <(O.O)  ", "  ( v )  ", "   |_|   ", "    ^    "}},
  {{"  *__*   ", " <(^.^)~ ", "  ( v )  ", "   |_|   ", "   ^^    "},
   {"  +__+   ", " <(^.^)~ ", "  ( v )  ", "   |_|   ", "    ^    "}},
}};

// Capybara
static const Species SP_CAPY = {"capybara", 0xC2A6, {
  {{"  ____   ", " (-.-)~  ", " /    \\  ", " |    |  ", " ^^  ^^  "},
   {"  ____   ", " (-.-)~  ", " /    \\  ", " |    |  ", " ^   ^   "}},
  {{"  ____   ", " (o.o)   ", " /    \\  ", " |    |  ", " ^^  ^^  "},
   {"  ____   ", " (-.o)   ", " /    \\  ", " |    |  ", " ^^  ^^  "}},
  {{"  ____   ", " (@.@)   ", " /    \\  ", " |    |  ", " *^*^*^* "},
   {"  ____   ", " (@.@)   ", " /    \\  ", " |    |  ", " ^*^*^*^ "}},
  {{" !____!  ", " (O.O)   ", " /    \\  ", " |    |  ", " ^^  ^^  "},
   {" !____!  ", " (O.O)   ", " /    \\  ", " |    |  ", " ^   ^   "}},
  {{" *____*  ", " (^o^)   ", " /    \\  ", " |    |  ", " ^^  ^^  "},
   {" +____+  ", "  (^o^)  ", " /    \\  ", " |    |  ", " ^^  ^^  "}},
}};

// Ghost
static const Species SP_GHOST = {"ghost", C_DIM, {
  {{"  ___   ", " /-.-\\  ", " | * | ", " | * | ", " ^^^^^^ "},
   {"  ___   ", " /-.-\\  ", " | * | ", " | * | ", " ~~~~~~ "}},
  {{"  ___   ", " /o.o\\  ", " |   | ", " |   | ", " ^^^^^^ "},
   {"  ___   ", " /-.o\\  ", " |   | ", " |   | ", " ^^^^^^ "}},
  {{"  ___   ", " /@.@\\  ", " | ~ | ", " | ~ | ", " ^^^^^^ "},
   {"  ___   ", " /@.@\\  ", " | _ | ", " | _ | ", " ^^^^^^ "}},
  {{" .!!!.  ", " /O.O\\  ", " |   | ", " |   | ", " ^^^^^^ "},
   {" .!!!.  ", " /O.O\\  ", " |~~~| ", " |   | ", " ^^^^^^ "}},
  {{" .***.  ", " /^.^\\  ", " |   | ", " |   | ", " ^^^^^^ "},
   {" .+++.  ", " /^.^\\  ", " | v | ", " |   | ", " ^^^^^^ "}},
}};

static const Species* SPECIES[] = { &SP_CAT, &SP_OWL, &SP_ROBOT, &SP_DUCK, &SP_CAPY, &SP_GHOST };
static const uint8_t N_SPECIES = sizeof(SPECIES) / sizeof(SPECIES[0]);
static uint8_t curSpecies = 0;

static int personaIdx(Persona p) {
  switch (p) {
    case P_SLEEP: return 0;
    case P_BUSY:  return 2;
    case P_ATTN:  return 3;
    case P_HAPPY: return 4;
    default:      return 1;  // IDLE
  }
}

static const char* const* petArt(Persona p, uint16_t* col, uint8_t frame) {
  const Species* sp = SPECIES[curSpecies];
  *col = sp->bodyCol;
  // Persona color override for emphasis
  switch (p) {
    case P_SLEEP: *col = C_DIM; break;
    case P_BUSY:  *col = C_CYAN; break;
    case P_ATTN:  *col = C_HOT; break;
    case P_HAPPY: *col = C_GREEN; break;
    default: break;
  }
  return sp->frames[personaIdx(p)][frame & 1].lines;
}

static void drawPet(int cx, int cy, const char* const* art, uint16_t color, uint8_t scale) {
  gfx->setTextSize(scale);
  gfx->setTextColor(color, C_BG);
  int charW = 6 * scale;
  int charH = 8 * scale;
  int lineH = charH + 4 * scale;
  // Clear pet area first to avoid frame artifacts
  int boxW = 11 * charW + 8;
  int boxH = 5 * lineH + 4;
  gfx->fillRect(cx - boxW / 2, cy - boxH / 2, boxW, boxH, C_BG);
  for (int i = 0; i < 5; i++) {
    int len = strlen(art[i]);
    int tw = len * charW;
    gfx->setCursor(cx - tw / 2, cy - (5 * lineH) / 2 + i * lineH);
    gfx->print(art[i]);
  }
}

// Animation frame counter (toggles ~1 Hz)
static uint8_t animFrame() {
  return (millis() / 800) & 1;
}

// Page header — label centered, swipe dots below
static void drawPageHeader(const char* label, int idx, int total) {
  gfx->fillRect(0, 0, W, 50, C_BG);
  gfx->setTextSize(3);
  gfx->setTextColor(C_TEXT, C_BG);
  int tw = strlen(label) * 18;
  gfx->setCursor(W / 2 - tw / 2, 14);
  gfx->print(label);

  // Page indicator dots
  int dotR = 6;
  int dotSp = 24;
  int dotsW = (total - 1) * dotSp;
  int sx = W / 2 - dotsW / 2;
  for (int i = 0; i < total; i++) {
    if (i == idx) gfx->fillCircle(sx + i * dotSp, 44, dotR, C_BODY);
    else gfx->drawCircle(sx + i * dotSp, 44, dotR, C_DIM);
  }
}

// ───── HOME page ─────
// Layout matches official buddy: big animated pet centered, status above,
// transcript HUD below.
static void drawHome() {
  gfx->fillScreen(C_BG);
  drawPageHeader("HOME", 0, M_COUNT);

  // Status strip below header
  bool live = state.connected;
  uint16_t hdrBg = live ? 0x0320 : C_PANEL;
  gfx->fillRoundRect(60, 70, W - 120, 38, 6, hdrBg);
  gfx->setTextSize(2);
  gfx->setTextColor(live ? C_GREEN : C_HOT, hdrBg);
  gfx->setCursor(80, 80);
  if (live) {
    gfx->printf("Claude  |  Sess %u  Run %u  Wait %u  Tok %lu",
      state.sessionsTotal, state.sessionsRunning, state.sessionsWaiting,
      (unsigned long)state.tokensToday);
  } else {
    gfx->printf("DEMO MODE  |  %s", state.msg);
  }

  // Big pet center
  Persona p = derive();
  uint16_t petCol;
  const char* const* art = petArt(p, &petCol, animFrame());
  drawPet(W / 2, 230, art, petCol, 7);

  // Owner / pet name under pet
  gfx->setTextSize(3);
  gfx->setTextColor(C_BODY, C_BG);
  char label[64];
  if (state.ownerName[0]) {
    snprintf(label, sizeof(label), "%s's %s", state.ownerName, state.petName);
  } else {
    snprintf(label, sizeof(label), "%s", state.petName);
  }
  int lw = strlen(label) * 18;
  gfx->setCursor(W / 2 - lw / 2, 350);
  gfx->print(label);

  // HUD: latest message + transcript
  if (state.connected) {
    int hudY = 388;
    gfx->setTextSize(1);
    gfx->setTextColor(C_TEXT, C_BG);
    gfx->setCursor(40, hudY);
    gfx->printf("%.90s", state.msg);
    hudY += 14;

    gfx->setTextColor(C_DIM, C_BG);
    uint8_t n = state.nLines > 3 ? 3 : state.nLines;
    for (uint8_t i = 0; i < n; i++) {
      gfx->setCursor(40, hudY);
      gfx->printf("%.90s", state.lines[i]);
      hudY += 12;
    }
  }

  // Footer
  gfx->fillRoundRect(20, H - 32, W - 40, 24, 6, C_PANEL);
  gfx->setTextSize(1);
  gfx->setTextColor(C_DIM, C_PANEL);
  gfx->setCursor(36, H - 26);
  const char* ls = linkConnected() ? "linked" :
    linkState == LINK_WS_CONNECTING ? "ws.." :
    linkState == LINK_WIFI_CONNECTING ? "wifi.." : "boot";
  gfx->printf("OK:%lu  Deny:%lu  WiFi:%s  IP:%s  Server:%s",
    (unsigned long)state.approvals, (unsigned long)state.denials,
    linkSsid(), linkIpStr()[0] ? linkIpStr() : "-", ls);
}

// Touch zones on PET page
static const Zone Z_PET_PREV_SPECIES = { 30, 140, 80, 200 };
static const Zone Z_PET_NEXT_SPECIES = { W - 110, 140, 80, 200 };

// ───── PET page ─────
static void drawPetPage() {
  gfx->fillScreen(C_BG);
  drawPageHeader("PET", 1, M_COUNT);

  // Pet (smaller, left), stats (right)
  Persona p = derive();
  uint16_t petCol;
  const char* const* art = petArt(p, &petCol, animFrame());
  drawPet(220, 240, art, petCol, 6);

  // Species name + cycle hints
  const Species* sp = SPECIES[curSpecies];
  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM, C_BG);
  gfx->setCursor(50, 220); gfx->print("<");
  gfx->setCursor(W / 2 - 100, 360);
  gfx->setTextColor(C_BODY, C_BG);
  char sbuf[32];
  snprintf(sbuf, sizeof(sbuf), "%s (%u/%u)", sp->name, curSpecies + 1, N_SPECIES);
  int sw = strlen(sbuf) * 12;
  gfx->setCursor(220 - sw / 2, 360);
  gfx->print(sbuf);
  gfx->setTextColor(C_DIM, C_BG);
  gfx->setCursor(W - 60, 220); gfx->print(">");

  // Stats panel right side
  int px = 440, py = 80, pw = 320, ph = 360;
  gfx->fillRoundRect(px, py, pw, ph, 12, C_PANEL);

  gfx->setTextSize(3);
  gfx->setTextColor(C_TEXT, C_PANEL);
  gfx->setCursor(px + 20, py + 20);
  char nameBuf[40];
  if (state.ownerName[0]) {
    snprintf(nameBuf, sizeof(nameBuf), "%s's %s", state.ownerName, state.petName);
  } else {
    snprintf(nameBuf, sizeof(nameBuf), "%s", state.petName);
  }
  gfx->printf("%.18s", nameBuf);

  // Mood bar (hearts based on approval ratio)
  int by = py + 80;
  uint32_t total_resp = state.approvals + state.denials;
  uint8_t mood = total_resp == 0 ? 2 :
    state.approvals * 4 / (total_resp == 0 ? 1 : total_resp);
  if (mood > 4) mood = 4;
  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM, C_PANEL);
  gfx->setCursor(px + 20, by); gfx->print("mood");
  for (int i = 0; i < 4; i++) {
    int hx = px + 130 + i * 30;
    if (i < mood) {
      gfx->fillCircle(hx - 5, by + 10, 5, C_HOT);
      gfx->fillCircle(hx + 5, by + 10, 5, C_HOT);
      gfx->fillTriangle(hx - 10, by + 12, hx + 10, by + 12, hx, by + 22, C_HOT);
    } else {
      gfx->drawCircle(hx - 5, by + 10, 5, C_DIM);
      gfx->drawCircle(hx + 5, by + 10, 5, C_DIM);
    }
  }
  by += 50;

  // Fed bar (10 dots, based on tokens)
  uint8_t fed = (state.tokensToday / 5000) % 11;
  gfx->setCursor(px + 20, by); gfx->print("fed");
  for (int i = 0; i < 10; i++) {
    int dx = px + 100 + i * 18;
    if (i < fed) gfx->fillCircle(dx, by + 10, 5, C_BODY);
    else gfx->drawCircle(dx, by + 10, 5, C_DIM);
  }
  by += 50;

  // Energy bar (5 boxes, drains over time)
  static uint32_t lastInteractMs = 0;
  if (total_resp > 0 || state.connected) lastInteractMs = millis();
  uint32_t hoursIdle = (millis() - lastInteractMs) / 3600000;
  int8_t en = 5 - (int8_t)(hoursIdle / 2);
  if (en < 0) en = 0; if (en > 5) en = 5;
  gfx->setCursor(px + 20, by); gfx->print("energy");
  for (int i = 0; i < 5; i++) {
    int ex = px + 130 + i * 30;
    if (i < en) gfx->fillRect(ex, by + 4, 24, 16, en >= 4 ? C_GREEN : (en >= 2 ? 0xFFE0 : C_HOT));
    else gfx->drawRect(ex, by + 4, 24, 16, C_DIM);
  }
  by += 50;

  // Level (tokens / 50000)
  uint8_t lvl = state.tokens / 50000;
  gfx->fillRoundRect(px + 20, by, 70, 30, 6, C_BODY);
  gfx->setTextSize(2);
  gfx->setTextColor(C_BG, C_BODY);
  gfx->setCursor(px + 30, by + 8);
  gfx->printf("Lv %u", lvl);
  by += 50;

  // Stats numbers
  gfx->setTextSize(2);
  gfx->setTextColor(C_GREEN, C_PANEL);
  gfx->setCursor(px + 20, by); gfx->printf("approved %lu", (unsigned long)state.approvals);
  by += 26;
  gfx->setTextColor(C_RED, C_PANEL);
  gfx->setCursor(px + 20, by); gfx->printf("denied   %lu", (unsigned long)state.denials);
  by += 26;
  gfx->setTextColor(C_DIM, C_PANEL);
  gfx->setCursor(px + 20, by); gfx->printf("tokens   %lu", (unsigned long)state.tokens);
}

// ───── INFO page ─────
static void drawInfoPage() {
  gfx->fillScreen(C_BG);
  drawPageHeader("INFO", 2, M_COUNT);

  int px = 60, py = 80;
  gfx->setTextSize(2);
  gfx->setTextColor(C_TEXT, C_BG);
  gfx->setCursor(px, py); gfx->print("CLAUDE BUDDY 7\""); py += 36;

  gfx->setTextSize(1);
  gfx->setTextColor(C_DIM, C_BG);
  gfx->setCursor(px, py); gfx->print("Tap top-left/right to switch pages."); py += 20;
  gfx->setCursor(px, py); gfx->print("On approval prompt: tap APPROVE / DENY."); py += 30;

  gfx->setTextColor(C_BODY, C_BG);
  gfx->setCursor(px, py); gfx->print("CONNECTION"); py += 20;
  gfx->setTextColor(C_DIM, C_BG);
  gfx->setCursor(px + 12, py); gfx->printf("WiFi: %s", linkSsid()); py += 16;
  gfx->setCursor(px + 12, py); gfx->printf("IP: %s", linkIpStr()[0] ? linkIpStr() : "-"); py += 16;
  gfx->setCursor(px + 12, py); gfx->printf("Server: ws://" WS_HOST ":%d" WS_PATH, WS_PORT); py += 16;
  gfx->setCursor(px + 12, py); gfx->printf("State: %s",
    linkConnected() ? "linked" : linkState == LINK_WS_CONNECTING ? "ws connecting" :
    linkState == LINK_WIFI_CONNECTING ? "wifi connecting" : "boot"); py += 30;

  gfx->setTextColor(C_BODY, C_BG);
  gfx->setCursor(px, py); gfx->print("STATS"); py += 20;
  gfx->setTextColor(C_DIM, C_BG);
  gfx->setCursor(px + 12, py); gfx->printf("Approvals: %lu", (unsigned long)state.approvals); py += 16;
  gfx->setCursor(px + 12, py); gfx->printf("Denials: %lu", (unsigned long)state.denials); py += 16;
  gfx->setCursor(px + 12, py); gfx->printf("Uptime: %lus", (unsigned long)(millis() / 1000)); py += 16;
  gfx->setCursor(px + 12, py); gfx->printf("Heap: %uKB", ESP.getFreeHeap() / 1024); py += 30;

  gfx->setTextColor(C_BODY, C_BG);
  gfx->setCursor(px, py); gfx->print("CREDITS"); py += 20;
  gfx->setTextColor(C_DIM, C_BG);
  gfx->setCursor(px + 12, py); gfx->print("Inspired by anthropics/claude-desktop-buddy"); py += 16;
  gfx->setCursor(px + 12, py); gfx->print("Hardware: Waveshare ESP32-S3-Touch-LCD-7.0");
}

// ───── Approval overlay ─────
static void drawApprovalFull() {
  gfx->fillScreen(C_BG);
  gfx->setTextSize(4);
  gfx->setTextColor(C_HOT, C_BG);
  const char* h = "APPROVE?";
  gfx->setCursor((W - (int)strlen(h) * 24) / 2, 30);
  gfx->print(h);
  // timer painted separately by loop()

  gfx->setTextSize(5);
  gfx->setTextColor(C_TEXT, C_BG);
  int tlen = strlen(state.promptTool);
  if (tlen > 16) tlen = 16;
  gfx->setCursor((W - tlen * 30) / 2, 140);
  gfx->printf("%.16s", state.promptTool);

  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM, C_BG);
  int hlen = strlen(state.promptHint);
  int hShow = hlen > 60 ? 60 : hlen;
  gfx->setCursor((W - hShow * 12) / 2, 220);
  gfx->printf("%.60s", state.promptHint);

  {
    uint16_t pcol;
    const char* const* pa = petArt(P_ATTN, &pcol, animFrame());
    drawPet(W / 2, 270, pa, pcol, 2);
  }

  gfx->fillRoundRect(Z_APPROVE.x, Z_APPROVE.y, Z_APPROVE.w, Z_APPROVE.h, 16, C_DARKG);
  gfx->drawRoundRect(Z_APPROVE.x, Z_APPROVE.y, Z_APPROVE.w, Z_APPROVE.h, 16, C_GREEN);
  gfx->setTextSize(4);
  gfx->setTextColor(C_GREEN, C_DARKG);
  gfx->setCursor(Z_APPROVE.x + 70, Z_APPROVE.y + 32);
  gfx->print("APPROVE");

  gfx->fillRoundRect(Z_DENY.x, Z_DENY.y, Z_DENY.w, Z_DENY.h, 16, C_DARKR);
  gfx->drawRoundRect(Z_DENY.x, Z_DENY.y, Z_DENY.w, Z_DENY.h, 16, C_RED);
  gfx->setTextSize(4);
  gfx->setTextColor(C_RED, C_DARKR);
  gfx->setCursor(Z_DENY.x + 110, Z_DENY.y + 32);
  gfx->print("DENY");
}

static void drawScreen(bool inPrompt) {
  if (inPrompt) {
    drawApprovalFull();
    return;
  }
  switch (dispMode) {
    case M_PET:  drawPetPage(); break;
    case M_INFO: drawInfoPage(); break;
    default:     drawHome(); break;
  }
}

// ───── WS event ─────
static void wsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.println("[ws] connected");
      linkState = LINK_LIVE;
      break;
    case WStype_DISCONNECTED:
      Serial.println("[ws] disconnected");
      linkState = LINK_WS_CONNECTING;
      break;
    case WStype_TEXT:
      protoFeed((const char*)payload, length, &state);
      break;
    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("[buddy] boot (WiFi mode)");

  ch422gInit(8, 9);
  Serial.println("[buddy] CH422G OK");

  persistLoad(&state.approvals, &state.denials,
              state.ownerName, sizeof(state.ownerName),
              state.petName, sizeof(state.petName));

  if (!gfx->begin()) {
    Serial.println("[buddy] gfx FAIL");
    while (1) delay(1000);
  }
  Serial.println("[buddy] display OK");

  gfx->fillScreen(C_BG);
  gfx->setTextSize(6);
  gfx->setTextColor(C_BODY, C_BG);
  gfx->setCursor(W / 2 - 130, H / 2 - 60);
  gfx->print("Hello!");
  gfx->setTextSize(3);
  gfx->setTextColor(C_DIM, C_BG);
  gfx->setCursor(W / 2 - 150, H / 2 + 20);
  gfx->print("Claude Buddy 7\"");

  linkInit(wsEvent);

  delay(1500);
  Serial.println("[buddy] ready");
}

void loop() {
  uint32_t now = millis();

  linkPoll();
  protoUpdateConnected(&state);
  demoTick();

  if (strcmp(state.promptId, lastPromptId) != 0) {
    strncpy(lastPromptId, state.promptId, sizeof(lastPromptId) - 1);
    lastPromptId[sizeof(lastPromptId) - 1] = 0;
    responseSent = false;
    if (state.promptId[0]) promptArrivedMs = now;
  }
  bool inPrompt = state.promptId[0] && !responseSent;

  // Touch poll @ 20Hz
  static uint32_t nextTouchPoll = 0;
  static bool wasTouching = false;
  static int touchStartX = 0, touchStartY = 0;
  static int lastTx = 0, lastTy = 0;
  int tx, ty;
  if (now >= nextTouchPoll) {
    nextTouchPoll = now + 50;
    bool got = gt911GetTouch(&tx, &ty);

    if (got && !wasTouching) {
      // Touch start
      touchStartX = tx; touchStartY = ty;
      lastTx = tx; lastTy = ty;
      Serial.printf("[touch start] %d,%d mode=%d inPrompt=%d\n", tx, ty, dispMode, inPrompt);
    } else if (got) {
      lastTx = tx; lastTy = ty;
    } else if (!got && wasTouching) {
      // Touch release — classify gesture
      int dx = lastTx - touchStartX;
      int dy = lastTy - touchStartY;
      int adx = dx < 0 ? -dx : dx;
      int ady = dy < 0 ? -dy : dy;
      bool isSwipe = adx >= SWIPE_MIN_DX && ady < SWIPE_MAX_DY;
      bool isTap = adx < 30 && ady < 30;
      Serial.printf("[touch end] dx=%d dy=%d swipe=%d tap=%d\n", dx, dy, isSwipe, isTap);

      if (now - lastTouchMs > 300) {
        lastTouchMs = now;
        if (inPrompt && isTap) {
          char cmd[96];
          if (inZone(lastTx, lastTy, Z_APPROVE)) {
            snprintf(cmd, sizeof(cmd),
              "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"once\"}", state.promptId);
            linkSend(cmd);
            responseSent = true;
            state.approvals++;
            persistSaveStats(state.approvals, state.denials);
          } else if (inZone(lastTx, lastTy, Z_DENY)) {
            snprintf(cmd, sizeof(cmd),
              "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"deny\"}", state.promptId);
            linkSend(cmd);
            responseSent = true;
            state.denials++;
            persistSaveStats(state.approvals, state.denials);
          }
        } else if (!inPrompt && isSwipe) {
          if (dx < 0) {
            dispMode = (DispMode)((dispMode + 1) % M_COUNT);
          } else {
            dispMode = (DispMode)((dispMode + M_COUNT - 1) % M_COUNT);
          }
        } else if (!inPrompt && isTap && dispMode == M_PET) {
          // Tap left/right of pet on PET page = cycle species
          if (inZone(lastTx, lastTy, Z_PET_PREV_SPECIES)) {
            curSpecies = (curSpecies + N_SPECIES - 1) % N_SPECIES;
          } else if (inZone(lastTx, lastTy, Z_PET_NEXT_SPECIES)) {
            curSpecies = (curSpecies + 1) % N_SPECIES;
          }
        }
      }
    }
    wasTouching = got;
  }

  // State-hash gating — only redraw when something actually changed
  static uint32_t lastHash = 0;
  static DispMode lastMode = (DispMode)-1;
  static bool lastInPrompt = false;
  static uint32_t approvalTimerSec = 0xFFFFFFFF;

  uint32_t hash = 0;
  hash = hash * 31 + (state.connected ? 1 : 0);
  hash = hash * 31 + state.sessionsTotal;
  hash = hash * 31 + state.sessionsRunning;
  hash = hash * 31 + state.sessionsWaiting;
  hash = hash * 31 + (state.recentlyCompleted ? 1 : 0);
  hash = hash * 31 + (state.tokensToday & 0xFFFF);
  hash = hash * 31 + state.lineGen;
  hash = hash * 31 + (state.approvals & 0xFFFF);
  hash = hash * 31 + (state.denials & 0xFFFF);
  hash = hash * 31 + linkState;
  hash = hash * 31 + animFrame();
  hash = hash * 31 + curSpecies;
  for (const char* p = state.msg; *p; p++) hash = hash * 31 + *p;

  bool needRedraw = (hash != lastHash) || (dispMode != lastMode) || (inPrompt != lastInPrompt);
  if (needRedraw) {
    lastHash = hash;
    lastMode = dispMode;
    lastInPrompt = inPrompt;
    approvalTimerSec = 0xFFFFFFFF;
    drawScreen(inPrompt);
    lastDrawMs = now;
  } else if (inPrompt) {
    // Only repaint timer band on approval screen
    uint32_t sec = (now - promptArrivedMs) / 1000;
    if (sec != approvalTimerSec) {
      approvalTimerSec = sec;
      gfx->fillRect(W / 2 - 160, 88, 320, 28, C_BG);
      gfx->setTextSize(2);
      gfx->setTextColor(sec > 10 ? C_HOT : C_DIM, C_BG);
      char buf[32];
      snprintf(buf, sizeof(buf), "%lus waiting", (unsigned long)sec);
      int tlen = strlen(buf);
      gfx->setCursor(W / 2 - tlen * 6, 94);
      gfx->print(buf);
    }
  }

  delay(20);
}
