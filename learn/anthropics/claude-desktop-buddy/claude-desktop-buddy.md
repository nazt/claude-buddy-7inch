# claude-desktop-buddy Learning Index

## Source
- **Origin**: ./origin/ → /tmp/claude-desktop-buddy
- **GitHub**: https://github.com/anthropics/claude-desktop-buddy

## Explorations

### 2026-05-04 09:55 (default)
- [Key Insights](2026-05-04/0955_KEY-INSIGHTS.md) — anti-flicker, persona, prompt flow, port mapping

### 2026-05-04 10:13 (deep, 5 agents)
- [Protocol](2026-05-04/1013_PROTOCOL.md) — full wire protocol spec, JSON examples, WiFi mapping
- [Quick Reference](2026-05-04/1013_QUICK-REFERENCE.md) — persona/mode/buttons/timing/colors

**Key insights:**
- BLE NUS encrypted with passkey display; WiFi port can drop crypto layer
- Sprite double-buffer + per-region clears + tick gating = no flicker
- Stats persist on milestones only (approval/denial/level-up/nap-end)
- Tap-twice confirm pattern for destructive actions (3s arm window)
- 12-second wake transition holds P_SLEEP after screen wake
