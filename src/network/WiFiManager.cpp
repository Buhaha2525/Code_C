// src/network/WiFiManager.cpp

#include "WiFiManager.h"

WiFiManager::WiFiManager()
    : _state(State::DISCONNECTED),
      _lastReconnectAttemptMs(0),
      _lastStatusPrintMs(0) {
}

void WiFiManager::begin() {
    Serial.println("[WiFiManager] Module initialisé.");

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);

    startConnection();
}

void WiFiManager::update() {
    if (WiFi.status() == WL_CONNECTED) {
        if (_state != State::CONNECTED) {
            handleConnected();
        }

        return;
    }

    if (_state == State::CONNECTED) {
        handleDisconnected();
    }

    const uint32_t now = millis();

    if (now - _lastReconnectAttemptMs >= 15000) {
        _lastReconnectAttemptMs = now;
        Serial.print("[WiFiManager] Tentative de connexion Wi-Fi à : ");
        Serial.println(AppSecrets::WiFiConfig::SSID);
        startConnection();
    }

    if (now - _lastStatusPrintMs >= 5000) {
        _lastStatusPrintMs = now;

        Serial.print("[WiFiManager] État Wi-Fi : ");
        Serial.print(getStateString());
        Serial.print(" | Code statut : ");
        Serial.println(WiFi.status());
    }
}

bool WiFiManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

WiFiManager::State WiFiManager::getState() const {
    return _state;
}

const char* WiFiManager::getStateString() const {
    return stateToString(_state);
}

IPAddress WiFiManager::getLocalIP() const {
    return WiFi.localIP();
}

int32_t WiFiManager::getRSSI() const {
    if (!isConnected()) {
        return 0;
    }

    return WiFi.RSSI();
}

void WiFiManager::startConnection() {
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    _state = State::CONNECTING;

    Serial.print("[WiFiManager] Connexion au Wi-Fi : ");
    Serial.println(AppSecrets::WiFiConfig::SSID);

    WiFi.begin(
        AppSecrets::WiFiConfig::SSID,
        AppSecrets::WiFiConfig::PASSWORD
    );
}

void WiFiManager::handleConnected() {
    _state = State::CONNECTED;

    Serial.println("----------------------------------------------");
Serial.println("[WiFiManager] Wi-Fi connecté.");

Serial.print("[WiFiManager] SSID : ");
Serial.println(WiFi.SSID());

Serial.print("[WiFiManager] Adresse IP : ");
Serial.println(WiFi.localIP());

Serial.print("[WiFiManager] Passerelle : ");
Serial.println(WiFi.gatewayIP());

Serial.print("[WiFiManager] Masque : ");
Serial.println(WiFi.subnetMask());

Serial.print("[WiFiManager] BSSID : ");
Serial.println(WiFi.BSSIDstr());

Serial.print("[WiFiManager] Signal RSSI : ");
Serial.print(WiFi.RSSI());
Serial.println(" dBm");


    Serial.println("----------------------------------------------");
}

void WiFiManager::handleDisconnected() {
    _state = State::DISCONNECTED;

    Serial.println("----------------------------------------------");
    Serial.println("[WiFiManager][WARN] Connexion Wi-Fi perdue.");
    Serial.println("[WiFiManager] Tentative de reconnexion automatique.");
    Serial.println("----------------------------------------------");
}

const char* WiFiManager::stateToString(State state) const {
    switch (state) {
        case State::DISCONNECTED:
            return "DISCONNECTED";

        case State::CONNECTING:
            return "CONNECTING";

        case State::CONNECTED:
            return "CONNECTED";

        default:
            return "UNKNOWN";
    }
}