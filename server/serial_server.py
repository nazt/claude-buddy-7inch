#!/usr/bin/env python3
"""
Claude Buddy serial server.

Pushes heartbeat snapshots over /dev/ttyACM0 every 2s.
Receives permission decisions back from device.

Usage:
  uv run --with pyserial serial_server.py
  uv run --with pyserial serial_server.py --port /dev/ttyACM0
"""

import argparse
import json
import sys
import time
import threading

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed. Run: uv run --with pyserial serial_server.py")
    sys.exit(1)


SCENARIOS = [
    {"label": "asleep", "total": 0, "running": 0, "waiting": 0,
     "msg": "Idle", "entries": []},
    {"label": "one idle", "total": 1, "running": 0, "waiting": 0,
     "msg": "1 session idle", "entries": ["10:42 reading file..."]},
    {"label": "busy", "total": 4, "running": 3, "waiting": 0,
     "msg": "3 sessions running",
     "entries": ["10:43 git push", "10:42 yarn build", "10:41 reading src/"]},
    {"label": "attention", "total": 2, "running": 1, "waiting": 1,
     "msg": "approve: Bash",
     "entries": ["10:43 waiting for approval", "10:42 reading config"]},
    {"label": "completed", "total": 1, "running": 0, "waiting": 0,
     "msg": "✓ done",
     "entries": ["10:43 done", "10:42 building..."], "completed": True},
]

state = {"owner": "Nat", "pet": "claude", "tokens": 0, "tokens_today": 0}
ser = None
lock = threading.Lock()
pending_prompt = None


def send(obj):
    line = json.dumps(obj) + "\n"
    with lock:
        try:
            n = ser.write(line.encode())
            ser.flush()
            print(f"[server->] {n}B {obj.get('cmd', obj.get('msg', '?'))}", flush=True)
        except Exception as e:
            print(f"[server] write fail: {e}", flush=True)


def reader_thread():
    """Read lines back from device, log permissions."""
    global pending_prompt
    buf = b""
    while True:
        try:
            chunk = ser.read(256)
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                line = line.strip()
                if not line:
                    continue
                try:
                    text = line.decode("utf-8", errors="replace")
                except Exception:
                    continue
                if text.startswith("{"):
                    try:
                        msg = json.loads(text)
                    except Exception:
                        print(f"[device->] {text}", flush=True)
                        continue
                    cmd = msg.get("cmd")
                    if cmd == "permission":
                        print(f"[server] DECISION {msg.get('decision')} for {msg.get('id')}", flush=True)
                        pending_prompt = None
                    elif msg.get("ack"):
                        print(f"[ack] {msg}", flush=True)
                    else:
                        print(f"[device->] {text}", flush=True)
                else:
                    # Plain log lines from device
                    print(f"[device] {text}", flush=True)
        except Exception as e:
            print(f"[server] read err: {e}", flush=True)
            time.sleep(0.5)


def main():
    global ser
    p = argparse.ArgumentParser()
    p.add_argument("--port", default="/dev/ttyACM0")
    p.add_argument("--baud", type=int, default=115200)
    args = p.parse_args()

    print(f"[server] opening {args.port} @ {args.baud}", flush=True)
    ser = serial.Serial(args.port, args.baud, timeout=0.5)
    time.sleep(0.5)

    threading.Thread(target=reader_thread, daemon=True).start()

    # Initial hello
    send({"cmd": "owner", "name": state["owner"]})
    send({"cmd": "name", "name": state["pet"]})

    idx = 0
    last_cycle = time.monotonic()
    while True:
        now = time.monotonic()
        if now - last_cycle >= 8:
            last_cycle = now
            idx = (idx + 1) % len(SCENARIOS)

        s = SCENARIOS[idx].copy()
        snap = {
            "total": s["total"],
            "running": s["running"],
            "waiting": s["waiting"],
            "msg": s["msg"],
            "entries": s["entries"],
            "tokens": state["tokens"],
            "tokens_today": state["tokens_today"],
            "owner": state["owner"],
            "pet": state["pet"],
            "completed": s.get("completed", False),
        }
        if s["label"] == "attention":
            snap["prompt"] = {
                "id": f"demo-{idx}-{int(now)}",
                "tool": "Bash",
                "hint": "rm -rf /tmp/foo",
            }
        send(snap)
        time.sleep(3)


if __name__ == "__main__":
    main()
