# Claude Desktop Buddy — Port Insights for 7" RGB

## Anti-Flicker (Critical Patterns)

1. **Sprite double-buffer** — all drawing in PSRAM, single `pushSprite()` per frame.
   Arduino_GFX RGB already has PSRAM framebuffer — drawing goes direct, no extra buffer needed BUT the LCD reads from this same buffer continuously, so partial writes can cause tearing.

2. **Tick gating** — animation at 5 Hz (200ms), loop runs at 60 Hz. Only redraw when:
   - Tick fired (`now >= nextTickAt`)
   - State changed (`lastDrawnState != current`)
   Otherwise skip drawing entirely.

3. **State caching** — `static uint8_t lastDrawnState = 0xFF;` skip redraws if state hash unchanged. Force via `invalidate()` setting cache to 0xFF.

4. **Per-region clears** — never `fillScreen()` during animation. Subsystems own non-overlapping regions:
   - Pet (top-left)
   - Status panels (right side)
   - Footer (bottom)
   - Approval overlay (modal, full screen on prompt)

5. **Text without ghosting** — `setTextColor(fg, bg)` so glyph paint atomically erases prior glyph at same position.

## Persona derivation

```cpp
PersonaState derive(state) {
  if (!connected)           return P_IDLE;     // disconnected = chill
  if (sessionsWaiting > 0)  return P_ATTENTION; // priority 1
  if (recentlyCompleted)    return P_CELEBRATE; // priority 2
  if (sessionsRunning >= 3) return P_BUSY;      // priority 3
  return P_IDLE;
}
```

Override layers: `oneShotUntil` (forced state for Nms), `wakeTransitionUntil` (12s sleep hold after wake).

## Prompt arrival

- Detect: `strcmp(state.promptId, lastPromptId) != 0`
- Force display mode = APPROVAL, close menus, beep, wake screen
- Track `promptArrivedMs` for response-time timer
- After response: `responseSent = true` → show "sent..." until next prompt

## Protocol JSON (WiFi port — same as BLE)

**Server → device** (heartbeat):
```json
{"total":3,"running":1,"waiting":1,"msg":"approve: Bash",
 "entries":["10:42 git push","10:41 yarn test"],
 "tokens":184502,"tokens_today":31200,
 "prompt":{"id":"req_abc","tool":"Bash","hint":"rm -rf /tmp"}}
```

**Server → device** (commands): `{"cmd":"owner","name":"Felix"}`, `{"cmd":"name","name":"Clawd"}`, `{"cmd":"status"}`

**Device → server** (decisions): `{"cmd":"permission","id":"req_abc","decision":"once"}` or `"deny"`

**Device → server** (status ack): `{"ack":"status","ok":true,"data":{"name":"...","stats":{...}}}`

## WiFi port deltas

- Drop: passkey, MTU negotiation, bond storage, base64 chunks
- Keep: same JSON shapes, 30s connection timeout, prompt flow
- Add: WiFi reconnect logic, mDNS/DNS resolve for server host
