// src/network/WiFiManager.h

#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include "../config/config.h"
#include "../config/secrets.h"

/*
   =========================================================
   MODULE : WiFiManager

   Rôle :
   - Gérer la connexion Wi-Fi de l'ESP32
   - Surveiller les pertes réseau
   - Reconnecter automatiquement
   - Préparer l'intégration future avec MQTT
   =========================================================
*/

class WiFiManager {
public:
    enum class State {
        DISCONNECTED,
        CONNECTING,
        CONNECTED
    };

    WiFiManager();

    void begin();
    void update();

    bool isConnected() const;

    State getState() const;
    const char* getStateString() const;

    IPAddress getLocalIP() const;
    int32_t getRSSI() const;

private:
    State _state;

    uint32_t _lastReconnectAttemptMs;
    uint32_t _lastStatusPrintMs;

    void startConnection();
    void handleConnected();
    void handleDisconnected();

    const char* stateToString(State state) const;
};