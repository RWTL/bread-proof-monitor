#include "home_assistant.h"

#include <PubSubClient.h>
#include <WiFi.h>

#include "proof_state.h"
#include "text_utils.h"

namespace {
WiFiClient mqttNetwork;
PubSubClient mqtt(mqttNetwork);
String deviceId;
unsigned long lastConnectAttempt = 0;
constexpr unsigned long kPublishIntervalMs = 15000;
constexpr unsigned long kReconnectIntervalMs = 10000;

String baseTopic() { return "proofmonitor/" + deviceId; }

void publishDiscovery(const char* id, const char* name, const char* unit,
                      const char* deviceClass, const char* stateClass) {
  String payload = "{\"name\":\"" + String(name) + "\",\"uniq_id\":\"" + deviceId + "_" + id +
                   "\",\"stat_t\":\"" + baseTopic() + "/state\",\"val_tpl\":\"{{ value_json." + id + " }}\"";
  if (strlen(unit)) payload += ",\"unit_of_meas\":\"" + String(unit) + "\"";
  if (strlen(deviceClass)) payload += ",\"dev_cla\":\"" + String(deviceClass) + "\"";
  if (strlen(stateClass)) payload += ",\"stat_cla\":\"" + String(stateClass) + "\"";
  payload += ",\"avty_t\":\"" + baseTopic() + "/availability\",\"dev\":{\"ids\":[\"" + deviceId +
             "\"],\"name\":\"Bread Proof Monitor\",\"mf\":\"Canterrain\",\"mdl\":\"ESP32 Proof Monitor\"}}";
  mqtt.publish(("homeassistant/sensor/" + deviceId + "/" + id + "/config").c_str(), payload.c_str(), true);
}

void publishAllDiscovery() {
  publishDiscovery("rise", "Rise", "%", "", "measurement");
  publishDiscovery("target", "Target Rise", "%", "", "measurement");
  publishDiscovery("temperature", "Temperature", "°F", "temperature", "measurement");
  publishDiscovery("humidity", "Humidity", "%", "humidity", "measurement");
  publishDiscovery("dough_height", "Dough Height", "mm", "distance", "measurement");
  publishDiscovery("elapsed_seconds", "Elapsed", "s", "duration", "measurement");
  publishDiscovery("status", "Status", "", "", "");
  publishDiscovery("stage", "Stage", "", "", "");
  publishDiscovery("profile", "Profile", "", "", "");
}

bool connectMqtt(const AppState& state) {
  if (state.mqttHost.isEmpty() || WiFi.status() != WL_CONNECTED) return false;
  mqtt.setServer(state.mqttHost.c_str(), state.mqttPort);
  const String clientId = deviceId + "-" + String(esp_random(), HEX);
  const String availability = baseTopic() + "/availability";
  bool connected = state.mqttUsername.isEmpty()
                       ? mqtt.connect(clientId.c_str(), availability.c_str(), 0, true, "offline")
                       : mqtt.connect(clientId.c_str(), state.mqttUsername.c_str(), state.mqttPassword.c_str(),
                                      availability.c_str(), 0, true, "offline");
  if (!connected) return false;
  mqtt.publish(availability.c_str(), "online", true);
  publishAllDiscovery();
  return true;
}
}  // namespace

void saveHomeAssistantConfig(Preferences& prefs, AppState& state, const String& host,
                             uint16_t port, const String& username, const String& password) {
  state.mqttHost = host;
  state.mqttPort = port == 0 ? 1883 : port;
  state.mqttUsername = username;
  if (!password.isEmpty()) state.mqttPassword = password;
  prefs.putString("mqttHost", state.mqttHost);
  prefs.putUShort("mqttPort", state.mqttPort);
  prefs.putString("mqttUser", state.mqttUsername);
  prefs.putString("mqttPass", state.mqttPassword);
}

void beginHomeAssistant(AppState& state) {
  deviceId = "proofmonitor_" + String(static_cast<uint32_t>(ESP.getEfuseMac()), HEX);
  mqtt.setBufferSize(1024);
  mqtt.setSocketTimeout(2);
  if (!state.mqttHost.isEmpty()) connectMqtt(state);
}

void updateHomeAssistant(AppState& state, bool forcePublish) {
  if (state.mqttHost.isEmpty() || WiFi.status() != WL_CONNECTED) return;
  if (!mqtt.connected()) {
    if (millis() - lastConnectAttempt < kReconnectIntervalMs) return;
    lastConnectAttempt = millis();
    if (!connectMqtt(state)) return;
    forcePublish = true;
  }
  mqtt.loop();
  if (!forcePublish && millis() - state.lastHomeAssistantPublishMillis < kPublishIntervalMs) return;

  String json = "{\"rise\":" + String(displayRisePercent(state), 0) +
                ",\"target\":" + String(displayedTargetRisePercent(state), 0) +
                ",\"temperature\":" + String(state.temperatureF, 1) +
                ",\"humidity\":" + String(state.humidityPercent, 1) +
                ",\"dough_height\":" + String(currentDoughHeightMm(state), 1) +
                ",\"elapsed_seconds\":" + String(currentElapsedSeconds(state)) +
                ",\"status\":\"" + jsonEscape(statusText(state)) +
                "\",\"stage\":\"" + jsonEscape(state.selectedStage) +
                "\",\"profile\":\"" + jsonEscape(state.selectedRecipe) + "\"}";
  mqtt.publish((baseTopic() + "/state").c_str(), json.c_str(), true);
  state.lastHomeAssistantPublishMillis = millis();
}
