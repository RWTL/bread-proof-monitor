#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "app_state.h"

void saveHomeAssistantConfig(Preferences& prefs, AppState& state, const String& host,
                             uint16_t port, const String& username, const String& password);
void beginHomeAssistant(AppState& state);
void updateHomeAssistant(AppState& state, bool forcePublish = false);
