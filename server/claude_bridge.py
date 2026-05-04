#!/usr/bin/env python3
"""
Claude Code session bridge → buddy WebSocket protocol.

Watches ~/.claude/projects/*/*.jsonl files, aggregates live activity,
pushes heartbeat snapshots to ESP32 buddy.

Usage:
  uv run --with websockets claude_bridge.py
"""

import asyncio
import glob
import json
import os
import time
import websockets
from pathlib import Path

CLAUDE_DIR = Path.home() / ".claude" / "projects"
PORT = 8765

clients = set()
sessions = {}  # session_id -> {file, mtime, tail_lines, tokens, last_msg, last_seen}


def find_sessions():
    """Find all session JSONL files, indexed by sessionId."""
    out = {}
    for p in CLAUDE_DIR.glob("*/*.jsonl"):
        try:
            mtime = p.stat().st_mtime
        except OSError:
            continue
        sid = p.stem
        out[sid] = {"file": str(p), "mtime": mtime}
    return out


def parse_line(line):
    """Extract useful fields from one JSONL event."""
    try:
        d = json.loads(line)
    except Exception:
        return None
    t = d.get("type")
    msg = d.get("message") or {}
    if not isinstance(msg, dict):
        return None
    content = msg.get("content") or []
    if isinstance(content, str):
        text = content
        kinds = ["text"]
    elif isinstance(content, list):
        text = ""
        kinds = []
        for c in content:
            if not isinstance(c, dict):
                continue
            kinds.append(c.get("type", "?"))
            if c.get("type") == "text":
                text += c.get("text", "")
            elif c.get("type") == "tool_use":
                tool = c.get("name", "?")
                text += f"[{tool}]"
    else:
        text = ""
        kinds = []
    usage = msg.get("usage") or {}
    out_tok = usage.get("output_tokens", 0) if isinstance(usage, dict) else 0
    return {
        "type": t,
        "kinds": kinds,
        "text": text,
        "out_tok": out_tok,
        "ts": d.get("timestamp"),
        "cwd": d.get("cwd"),
    }


def tail_session(file_path, max_events=20):
    """Read last N events from a session file (efficient tail)."""
    try:
        with open(file_path, "rb") as f:
            f.seek(0, 2)
            size = f.tell()
            chunk = min(size, 64 * 1024)
            f.seek(size - chunk)
            data = f.read().decode("utf-8", errors="replace")
        lines = [l for l in data.splitlines() if l.strip()]
        events = []
        for line in lines[-max_events:]:
            ev = parse_line(line)
            if ev:
                events.append(ev)
        return events
    except Exception as e:
        return []


def build_snapshot():
    """Aggregate session state → heartbeat snapshot."""
    now = time.time()
    sess_map = find_sessions()

    total = len(sess_map)
    running = 0
    cumulative_tokens = 0
    recent_entries = []  # (ts, label) sorted by ts desc
    latest_msg = "Idle"

    # Keep most recent N sessions for entries
    sess_sorted = sorted(sess_map.items(), key=lambda x: -x[1]["mtime"])

    for sid, info in sess_sorted[:8]:
        age = now - info["mtime"]
        if age < 30:
            running += 1
        events = tail_session(info["file"], max_events=5)
        for ev in events:
            cumulative_tokens += ev.get("out_tok", 0) or 0
            txt = ev.get("text", "").strip()
            if not txt:
                continue
            cwd = (ev.get("cwd") or "").split("/")[-1] or "?"
            short = txt.replace("\n", " ")[:60]
            recent_entries.append((ev.get("ts") or "", f"{cwd}: {short}"))
        if events and age < 60:
            last = events[-1]
            cwd = (last.get("cwd") or "").split("/")[-1] or "?"
            kinds = "+".join(last.get("kinds", []))
            latest_msg = f"{cwd} [{kinds}]"

    # Take 6 most recent entries by ts
    recent_entries.sort(key=lambda x: x[0], reverse=True)
    entries = [e[1] for e in recent_entries[:6]]

    return {
        "total": total,
        "running": running,
        "waiting": 0,  # We can't reliably detect approval prompts from logs alone
        "msg": latest_msg[:40],
        "entries": entries,
        "tokens": cumulative_tokens,
        "tokens_today": cumulative_tokens,
        "owner": "Nat",
        "pet": "claude",
    }


async def broadcast(obj):
    msg = json.dumps(obj) + "\n"
    for ws in list(clients):
        try:
            await ws.send(msg)
        except Exception:
            clients.discard(ws)


async def heartbeat_loop():
    while True:
        try:
            snap = build_snapshot()
            await broadcast(snap)
            print(f"[bridge] {snap['total']} sess, {snap['running']} running, "
                  f"{snap['tokens']} tok, msg={snap['msg']}", flush=True)
        except Exception as e:
            print(f"[bridge] err: {e}", flush=True)
        await asyncio.sleep(2)


async def handle_client(ws):
    clients.add(ws)
    print(f"[bridge] client connected from {ws.remote_address}", flush=True)
    await ws.send(json.dumps({"cmd": "owner", "name": "Nat"}) + "\n")
    await ws.send(json.dumps({"cmd": "name", "name": "claude"}) + "\n")
    try:
        async for raw in ws:
            line = raw.strip() if isinstance(raw, str) else raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue
            print(f"[device->] {line}", flush=True)
    except websockets.ConnectionClosed:
        pass
    finally:
        clients.discard(ws)
        print("[bridge] client disconnected", flush=True)


async def main():
    print(f"[bridge] watching {CLAUDE_DIR}", flush=True)
    print(f"[bridge] WS server on :{PORT}/buddy", flush=True)
    async with websockets.serve(handle_client, "0.0.0.0", PORT):
        await heartbeat_loop()


if __name__ == "__main__":
    asyncio.run(main())
