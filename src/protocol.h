#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "wifi_link.h"

struct ClaudeState {
  uint8_t  sessionsTotal   = 0;
  uint8_t  sessionsRunning = 0;
  uint8_t  sessionsWaiting = 0;
  bool     recentlyCompleted = false;
  uint32_t tokens = 0;
  uint32_t tokensToday = 0;
  uint32_t lastUpdated = 0;
  char     msg[48] = "Waiting for server...";
  bool     connected = false;
  char     lines[8][92];
  uint8_t  nLines = 0;
  uint16_t lineGen = 0;
  char     promptId[40] = "";
  char     promptTool[32] = "";
  char     promptHint[64] = "";
  char     ownerName[24] = "";
  char     petName[24] = "oracle";
  uint32_t approvals = 0;
  uint32_t denials = 0;
};

static uint32_t _lastLiveMs = 0;

inline bool protoConnected() {
  return _lastLiveMs != 0 && (millis() - _lastLiveMs) <= 30000;
}

inline void protoApplyJson(const char* line, ClaudeState* s) {
  JsonDocument doc;
  if (deserializeJson(doc, line)) {
    Serial.printf("[proto] parse FAIL: %.80s\n", line);
    return;
  }

  const char* cmd = doc["cmd"];
  if (cmd) {
    if (strcmp(cmd, "owner") == 0) {
      const char* n = doc["name"];
      if (n) { strncpy(s->ownerName, n, sizeof(s->ownerName)-1); s->ownerName[sizeof(s->ownerName)-1] = 0; }
      _lastLiveMs = millis();
      return;
    }
    if (strcmp(cmd, "name") == 0) {
      const char* n = doc["name"];
      if (n) { strncpy(s->petName, n, sizeof(s->petName)-1); s->petName[sizeof(s->petName)-1] = 0; }
      _lastLiveMs = millis();
      return;
    }
    _lastLiveMs = millis();
    return;
  }

  // Heartbeat snapshot
  s->sessionsTotal     = doc["total"]     | s->sessionsTotal;
  s->sessionsRunning   = doc["running"]   | s->sessionsRunning;
  s->sessionsWaiting   = doc["waiting"]   | s->sessionsWaiting;
  s->recentlyCompleted = doc["completed"] | false;
  s->tokens            = doc["tokens"]    | s->tokens;
  s->tokensToday       = doc["tokens_today"] | s->tokensToday;

  const char* m = doc["msg"];
  if (m) { strncpy(s->msg, m, sizeof(s->msg)-1); s->msg[sizeof(s->msg)-1] = 0; }

  const char* o = doc["owner"];
  if (o) { strncpy(s->ownerName, o, sizeof(s->ownerName)-1); s->ownerName[sizeof(s->ownerName)-1] = 0; }
  const char* pn = doc["pet"];
  if (pn) { strncpy(s->petName, pn, sizeof(s->petName)-1); s->petName[sizeof(s->petName)-1] = 0; }

  JsonArray la = doc["entries"];
  if (!la.isNull()) {
    uint8_t n = 0;
    for (JsonVariant v : la) {
      if (n >= 8) break;
      const char* str = v.as<const char*>();
      strncpy(s->lines[n], str ? str : "", 91); s->lines[n][91] = 0;
      n++;
    }
    if (n != s->nLines) s->lineGen++;
    s->nLines = n;
  }

  JsonObject pr = doc["prompt"];
  if (!pr.isNull()) {
    const char* pid = pr["id"]; const char* pt = pr["tool"]; const char* ph = pr["hint"];
    strncpy(s->promptId,   pid ? pid : "", sizeof(s->promptId)-1);   s->promptId[sizeof(s->promptId)-1] = 0;
    strncpy(s->promptTool, pt  ? pt  : "", sizeof(s->promptTool)-1); s->promptTool[sizeof(s->promptTool)-1] = 0;
    strncpy(s->promptHint, ph  ? ph  : "", sizeof(s->promptHint)-1); s->promptHint[sizeof(s->promptHint)-1] = 0;
  } else {
    s->promptId[0] = 0; s->promptTool[0] = 0; s->promptHint[0] = 0;
  }

  s->lastUpdated = millis();
  _lastLiveMs = millis();
}

inline void protoFeed(const char* data, size_t len, ClaudeState* s) {
  static char buf[2048];
  if (len >= sizeof(buf)) len = sizeof(buf) - 1;
  memcpy(buf, data, len);
  buf[len] = 0;
  if (buf[0] == '{') protoApplyJson(buf, s);
}

inline void protoUpdateConnected(ClaudeState* s) {
  s->connected = (linkState == LINK_LIVE) && protoConnected();
  if (!s->connected) {
    s->sessionsTotal = 0; s->sessionsRunning = 0; s->sessionsWaiting = 0;
    s->recentlyCompleted = false;
  }
}
