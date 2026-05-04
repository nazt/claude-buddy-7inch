#!/usr/bin/env python3
"""
Claude Buddy WebSocket server (mock + ready for real wiring).

Runs on white.local:8765, path /buddy.
Pushes heartbeat snapshots every 2s. Receives permission decisions from device.

Usage:
  uv run --with websockets server.py
  # or
  python3 -m pip install websockets && python3 server.py
"""

import asyncio
import json
import time
import websockets

clients = set()
state = {
    "total": 0, "running": 0, "waiting": 0,
    "msg": "Idle", "entries": [], "tokens": 0, "tokens_today": 0,
    "owner": "Nat", "pet": "claude",
}
pending_prompt = None  # {"id": ..., "tool": ..., "hint": ...}


SCENARIOS = [
    {"label": "asleep", "total": 0, "running": 0, "waiting": 0, "msg": "Idle", "entries": []},
    {"label": "one idle", "total": 1, "running": 0, "waiting": 0, "msg": "1 session idle",
     "entries": ["10:42 reading file..."]},
    {"label": "busy", "total": 4, "running": 3, "waiting": 0, "msg": "3 sessions running",
     "entries": ["10:43 git push", "10:42 yarn build", "10:41 reading src/"]},
    {"label": "attention", "total": 2, "running": 1, "waiting": 1, "msg": "approve: Bash",
     "entries": ["10:43 waiting for approval", "10:42 reading config"]},
    {"label": "completed", "total": 1, "running": 0, "waiting": 0, "msg": "✓ done",
     "entries": ["10:43 done", "10:42 building..."], "completed": True},
]


async def broadcast(obj):
    msg = json.dumps(obj) + "\n"
    dead = []
    for ws in list(clients):
        try:
            await ws.send(msg)
        except Exception:
            dead.append(ws)
    for ws in dead:
        clients.discard(ws)


async def heartbeat_loop():
    """Cycle through demo scenarios every 8s, push heartbeat every 2s."""
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
        if pending_prompt:
            snap["prompt"] = pending_prompt
        elif s["label"] == "attention":
            snap["prompt"] = {
                "id": f"demo-{idx}-{int(now)}",
                "tool": "Bash",
                "hint": "rm -rf /tmp/foo",
            }
        await broadcast(snap)
        await asyncio.sleep(2)


async def handle_client(ws):
    clients.add(ws)
    print(f"[server] client connected from {ws.remote_address}")

    # Hello: send owner + initial snapshot
    await ws.send(json.dumps({"cmd": "owner", "name": state["owner"]}) + "\n")
    await ws.send(json.dumps({"cmd": "name", "name": state["pet"]}) + "\n")

    try:
        async for raw in ws:
            line = raw.strip()
            if not line:
                continue
            print(f"[device->] {line}")
            try:
                msg = json.loads(line)
            except json.JSONDecodeError:
                continue

            cmd = msg.get("cmd")
            if cmd == "permission":
                pid = msg.get("id")
                decision = msg.get("decision")
                print(f"[server] permission {decision} for {pid}")
                global pending_prompt
                pending_prompt = None
            elif msg.get("ack"):
                print(f"[server] ack: {msg}")
    except websockets.ConnectionClosed:
        pass
    finally:
        clients.discard(ws)
        print(f"[server] client disconnected")


async def main():
    print("[server] Claude Buddy WS server on :8765/buddy")
    async with websockets.serve(handle_client, "0.0.0.0", 8765, process_request=None):
        await heartbeat_loop()


if __name__ == "__main__":
    asyncio.run(main())
