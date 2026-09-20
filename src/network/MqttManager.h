// src/network/MqttManager.h

#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#include "../config/config.h"
#include "../config/secrets.h"
#include "../config/MachineIdentity.h"

class MqttManager {
public:
    enum class State {
        DISCONNECTED,
        CONNECTING,
        CONNECTED
    };

    using MessageCallback = void (*)(const char* topic, const char* payload);

    MqttManager();

    void begin();
    void update();

    bool isConnected();

    bool publish(
        const char* topic,
        const char* payload,
        bool retained = false
    );

    void setMessageCallback(MessageCallback callback);

    State getState() const;
    const char* getStateString() const;

private:
    WiFiClient _wifiClient;
    WiFiClientSecure _secureClient;
    PubSubClient _client;

    State _state;

    uint32_t _lastReconnectAttemptMs;
    uint32_t _lastStatusPrintMs;

    MessageCallback _messageCallback;

    static MqttManager* _instance;

    void configureClient();
    void configureTls();
    void connect();
    void handleConnected();
    void handleDisconnected();

    void subscribeToTopics();

    static void mqttCallbackStatic(
        char* topic,
        byte* payload,
        unsigned int length
    );

    void handleIncomingMessage(
        char* topic,
        byte* payload,
        unsigned int length
    );

    const char* stateToString(State state) const;
};