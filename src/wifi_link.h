#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include "secrets.h"

extern WebSocketsClient webSocket;

enum LinkState { LINK_BOOT, LINK_WIFI_CONNECTING, LINK_WS_CONNECTING, LINK_LIVE };
extern LinkState linkState;

typedef void (*LinkEventHandler)(WStype_t type, uint8_t* payload, size_t length);
void linkInit(LinkEventHandler eventHandler);
void linkPoll();
bool linkConnected();
void linkSend(const char* json);
const char* linkIpStr();
const char* linkSsid();
