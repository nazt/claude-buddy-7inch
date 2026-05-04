#include "wifi_link.h"

WebSocketsClient webSocket;
LinkState linkState = LINK_BOOT;

static IPAddress wsIp;
static char ipStr[24] = "";
static uint32_t lastReconnect = 0;
static uint32_t lastDbgMs = 0;

static void wifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[wifi] STA_CONNECTED");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("[wifi] GOT_IP %s\n", WiFi.localIP().toString().c_str());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.printf("[wifi] DISCONNECTED reason=%d\n", info.wifi_sta_disconnected.reason);
      break;
    default:
      Serial.printf("[wifi] event %d\n", event);
      break;
  }
}

void linkInit(LinkEventHandler eventHandler) {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("claude-buddy");
  WiFi.onEvent(wifiEvent);
  WiFi.disconnect(true, true);
  delay(100);

  // Scan first to confirm SSID visible
  Serial.println("[wifi] scanning...");
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n && i < 12; i++) {
    Serial.printf("[wifi]   %d) %s rssi=%d enc=%d\n",
      i, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.encryptionType(i));
  }
  WiFi.scanDelete();

  Serial.printf("[wifi] connect to '%s' pw len=%d\n", WIFI_SSID, (int)strlen(WIFI_PASS));
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  linkState = LINK_WIFI_CONNECTING;

  webSocket.onEvent(eventHandler);
  webSocket.setReconnectInterval(3000);
}

static void connectWS() {
  if (wsIp.fromString(WS_HOST)) {
    Serial.printf("[ws] connecting to %s:%d%s\n", WS_HOST, WS_PORT, WS_PATH);
  } else if (WiFi.hostByName(WS_HOST, wsIp) == 1) {
    Serial.printf("[dns] %s -> %s\n", WS_HOST, wsIp.toString().c_str());
  } else {
    Serial.printf("[dns] FAILED %s\n", WS_HOST);
    return;
  }
  webSocket.begin(wsIp.toString(), WS_PORT, WS_PATH);
  linkState = LINK_WS_CONNECTING;
}

void linkPoll() {
  uint32_t now = millis();

  // Periodic debug
  if (now - lastDbgMs > 5000) {
    lastDbgMs = now;
    Serial.printf("[link] state=%d wifi=%d ip=%s\n",
      linkState, WiFi.status(), WiFi.localIP().toString().c_str());
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (linkState == LINK_LIVE || linkState == LINK_WS_CONNECTING) {
      Serial.printf("[wifi] dropped (status=%d)\n", WiFi.status());
      linkState = LINK_WIFI_CONNECTING;
    }
    if (now - lastReconnect > 5000) {
      lastReconnect = now;
      WiFi.reconnect();
    }
    return;
  }

  // WiFi up — first time?
  if (linkState == LINK_WIFI_CONNECTING) {
    snprintf(ipStr, sizeof(ipStr), "%s", WiFi.localIP().toString().c_str());
    Serial.printf("[wifi] connected, ip=%s rssi=%d\n", ipStr, WiFi.RSSI());
    connectWS();
  }

  webSocket.loop();
}

bool linkConnected() { return linkState == LINK_LIVE; }

void linkSend(const char* json) {
  if (linkState != LINK_LIVE) {
    Serial.printf("[ws] send dropped (state=%d): %s\n", linkState, json);
    return;
  }
  webSocket.sendTXT(json);
  Serial.printf("[ws->] %s\n", json);
}

const char* linkIpStr() { return ipStr; }
const char* linkSsid() { return WIFI_SSID; }
