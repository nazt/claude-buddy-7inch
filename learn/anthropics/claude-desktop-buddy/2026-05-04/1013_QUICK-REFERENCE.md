# Quick Reference

## Persona States
| State | Trigger | Notes |
|-------|---------|-------|
| P_SLEEP | disconnected / night / face-down nap / 12s wake hold | breathing |
| P_IDLE | connected, no urgency | default |
| P_BUSY | running ≥ 3 | concurrent |
| P_ATTENTION | waiting > 0 | LED pulses red |
| P_CELEBRATE | recentlyCompleted, level-up (3s one-shot) | confetti |
| P_DIZZY | shake (>0.8Δ accel, 2s one-shot) | wobble |
| P_HEART | approval response < 5s (2s one-shot) | reward |

## Display Modes
- DISP_NORMAL — pet + HUD/transcript + approval overlay
- DISP_PET — 2 pages (stats / how-to)
- DISP_INFO — 6 pages (about/buttons/claude/device/bluetooth/credits)

A cycles modes, B pages within. Hold A = menu.

## Buttons (M5StickC Plus)
| Btn | Normal | Approval | Menu | Settings |
|-----|--------|----------|------|----------|
| A | next mode | approve | next item | next |
| B | scroll transcript | deny | confirm | toggle |
| Hold A | menu | menu | — | — |
| Power tap | screen off | — | — | — |
| Shake | dizzy | — | — | — |
| Face-down | nap | — | — | — |

## Timing
- TICK_MS = 200 (5fps animation)
- SCREEN_OFF_MS = 30000 (USB power exempt)
- WAKE_TRANSITION = 12000 (sleep hold after wake)
- TOKENS_PER_LEVEL = 50000
- ANIM_PAUSE_MS = 800 (between GIF variants)
- VARIANT_DWELL_MS = 5000

## NVS keys (namespace "buddy")
petname, owner, species (0xFF=GIF), nap, appr, deny, vel (8×u16 ring), vidx, vcnt, lvl, tok, s_snd, s_bt, s_wifi, s_led, s_hud, s_crot

## Stat formulas
- mood tier (0-4): median velocity + denial ratio penalty
- energy tier (0-5): drains 1/2hrs after wake, full after nap
- fed pips (0-9): `(tokens % 50K) / 5K`
- battery %: `(mV - 3200) / 10` clamped

## 18 ASCII Species
capybara, duck, goose, blob, cat, dragon, octopus, owl, penguin, turtle, snail, ghost, axolotl, cactus, robot, rabbit, mushroom, chonk

## Colors (RGB565)
HOT=0xFA20, PANEL=0x2104, GREEN=0x07E0, RED=0xF800, BUDDY_BG=0x0000, etc.

## Key flows
1. **First boot**: Hello! / a buddy appears (1.8s splash)
2. **Pairing**: 6-digit passkey on screen, beep 1800/60ms
3. **Prompt arrival**: beep 1200/80ms, force NORMAL, wake, invalidate, P_ATTENTION
4. **Approve <5s**: send `permission once`, statsOnApproval(s), beep 2400/60ms, P_HEART 2s
5. **Deny**: send `permission deny`, statsOnDenial, beep 600/60ms
6. **Reset confirm**: tap once → "really?" armed 3s → tap same item → execute
7. **Nap**: face-down 15 frames → ScreenBreath(8) → energy refills → wake on lift
8. **Charging clock**: USB + RTC valid + idle → time-of-day mood cycling
