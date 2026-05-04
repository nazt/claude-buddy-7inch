# Claude Desktop Buddy — Wire Protocol Spec

## Transport (BLE)
- NUS: Service `6e400001-b5a3-f393-e0a9-e50e24dcca9e`, RX `..02`, TX `..03`
- Encryption: ESP_BLE_SEC_ENCRYPT_MITM, DisplayOnly capability
- Format: UTF-8 JSON, `\n`-delimited, one obj per line
- MTU: request 517 (macOS = 185), chunk size = `mtu - 3`, cap 180

## Heartbeat (server → device, on change + 10s keepalive)
```json
{"total":3,"running":1,"waiting":1,"msg":"approve: Bash",
 "entries":["10:42 git push","10:41 yarn test"],
 "tokens":184502,"tokens_today":31200,
 "prompt":{"id":"req_abc","tool":"Bash","hint":"rm -rf /tmp"}}
```
30s timeout = dead connection. Optional: `completed:true` triggers P_CELEBRATE.

## On-connect (server → device)
- `{"time":[1775731234,-25200]}` — epoch + tz_offset_sec → set RTC
- `{"cmd":"owner","name":"Felix"}` → device acks `{"ack":"owner","ok":true}`

## Commands (server → device)

| Cmd | Payload | Ack |
|-----|---------|-----|
| `status` | — | full data: name/owner/sec/bat/sys/stats |
| `name` | `{name}` | `{"ack":"name","ok":true}` |
| `owner` | `{name}` | `{"ack":"owner","ok":true}` |
| `unpair` | — | `{"ack":"unpair","ok":true}` (clear bonds) |
| `species` | `{idx:0..17 or 0xFF}` | `{"ack":"species","ok":true}` |
| `char_begin` | `{name,total}` | fit check; wipe `/characters/`; `{ok,error?}` |
| `file` | `{path,size}` | open file; ack |
| `chunk` | `{d:"<base64>"}` | decode (cap 300B), append, `{ok,n:bytesWritten}` |
| `file_end` | — | close, verify size, `{ok,n:finalSize}` |
| `char_end` | — | reload character, `{ok}` |

## Status ack data
```json
{"name":"Clawd","owner":"Felix","sec":true,
 "bat":{"pct":87,"mV":4012,"mA":-120,"usb":true},
 "sys":{"up":8412,"heap":84200,"fsFree":102400,"fsTotal":1048576},
 "stats":{"appr":42,"deny":3,"vel":8,"nap":12,"lvl":5}}
```

## Permission decision (device → server)
```json
{"cmd":"permission","id":"<exact match>","decision":"once"|"deny"}
```
No ack. Server forwards to session manager.

## WiFi/WS port mapping

**Drop**: BLE pairing, passkey, MTU, bond storage, base64 cap, CCCD descriptor.

**Keep**: All JSON shapes, command names, 30s heartbeat timeout, prompt flow, status ack format.

**Reimplement**:
- Auth → device UUID + shared secret (or rely on local network trust)
- Discovery → mDNS for server host or hardcoded IP
- Transport security → optional TLS / wss://
- Persistent recognition → server keeps device ID list

## Examples
- Heartbeat / time / owner / status / permission / char_begin: see Protocol section above

## Implementation gotchas
- ArduinoJson `field | default` keeps previous value on missing key
- All strncpy with bounds: `strncpy(dst, src, sizeof(dst)-1); dst[sizeof(dst)-1]=0;`
- `lineGen` bumps when entries change → UI resets scroll
- Token count: latch first sight (`_tokensSynced`), accumulate delta only
- Save NVS only on milestones (level-up, approval, denial, nap end)
