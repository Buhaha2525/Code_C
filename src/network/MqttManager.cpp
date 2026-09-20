// src/network/MqttManager.cpp

#include "MqttManager.h"

MqttManager* MqttManager::_instance = nullptr;

MqttManager::MqttManager()
    : _client(),
      _state(State::DISCONNECTED),
      _lastReconnectAttemptMs(0),
      _lastStatusPrintMs(0),
      _messageCallback(nullptr) {
}

void MqttManager::begin() {
    Serial.println("[MqttManager] Module initialisé.");

    _instance = this;

    configureClient();

    _client.setServer(
        AppSecrets::MqttConfig::HOST,
        AppSecrets::MqttConfig::PORT
    );

    _client.setCallback(MqttManager::mqttCallbackStatic);
    _client.setBufferSize(AppConfig::Limits::MQTT_PAYLOAD_MAX_SIZE);

    connect();
}

void MqttManager::update() {
    if (WiFi.status() != WL_CONNECTED) {
        if (_state == State::CONNECTED) {
            handleDisconnected();
        }

        return;
    }

    if (_client.connected()) {
        if (_state != State::CONNECTED) {
            handleConnected();
        }

        _client.loop();
        return;
    }

    if (_state == State::CONNECTED) {
        handleDisconnected();
    }

    const uint32_t now = millis();

    if (now - _lastReconnectAttemptMs >= AppConfig::Timing::MQTT_RETRY_DELAY_MS) {
        _lastReconnectAttemptMs = now;
        connect();
    }

    if (now - _lastStatusPrintMs >= 5000) {
        _lastStatusPrintMs = now;

        Serial.print("[MqttManager] État MQTT : ");
        Serial.println(getStateString());
    }
}

bool MqttManager::isConnected() {
    return _client.connected();
}

bool MqttManager::publish(
    const char* topic,
    const char* payload,
    bool retained
) {
    if (!_client.connected()) {
        Serial.println("[MqttManager][WARN] Publication impossible : MQTT non connecté.");
        return false;
    }

    if (topic == nullptr || payload == nullptr) {
        Serial.println("[MqttManager][WARN] Topic ou payload invalide.");
        return false;
    }

    const bool success = _client.publish(topic, payload, retained);

    if (!success) {
        Serial.println("[MqttManager][ERREUR] Échec publication MQTT.");
        return false;
    }

    Serial.print("[MqttManager] Message publié sur : ");
    Serial.println(topic);

    return true;
}

void MqttManager::setMessageCallback(MessageCallback callback) {
    _messageCallback = callback;
}

MqttManager::State MqttManager::getState() const {
    return _state;
}

const char* MqttManager::getStateString() const {
    return stateToString(_state);
}

void MqttManager::configureClient() {
    if (AppSecrets::MqttConfig::USE_TLS) {
        configureTls();
        _client.setClient(_secureClient);
        Serial.println("[MqttManager] Mode MQTT : TLS sécurisé.");
    } else {
        _client.setClient(_wifiClient);
        Serial.println("[MqttManager] Mode MQTT : local sans TLS.");
    }
}

void MqttManager::configureTls() {
    if (AppConfig::Security::MQTT_USE_INSECURE_TLS_FOR_TEST) {
        _secureClient.setInsecure();
        Serial.println("[MqttManager][WARN] TLS en mode test : certificat non vérifié.");
        return;
    }

    _secureClient.setCACert(AppSecrets::Certificates::ROOT_CA);
    Serial.println("[MqttManager] Certificat CA configuré.");
}

void MqttManager::connect() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[MqttManager] Wi-Fi non connecté, connexion MQTT reportée.");
        return;
    }

    if (_client.connected()) {
        return;
    }

    _state = State::CONNECTING;

    // ✅ CLIENT_ID dynamique = ID machine
    const String clientId = machineIdentity.getId();

    Serial.print("[MqttManager] Connexion au broker MQTT : ");
    Serial.print(AppSecrets::MqttConfig::HOST);
    Serial.print(":");
    Serial.print(AppSecrets::MqttConfig::PORT);
    Serial.print(" (clientId=");
    Serial.print(clientId);
    Serial.println(")");

    bool connected = false;

    const bool hasCredentials =
        strlen(AppSecrets::MqttConfig::USERNAME) > 0;

    // ✅ Topics dynamiques
    const String statusTopic = machineIdentity.topicStatus();
    const String machineId   = machineIdentity.getId();

    char willPayload[256];
    snprintf(
        willPayload,
        sizeof(willPayload),
        "{\"type\":\"mqtt_status\",\"status\":\"OFFLINE\",\"isOnline\":false,\"machineId\":\"%s\",\"machineCode\":\"%s\",\"mac\":\"%s\"}",
        machineId.c_str(),
        machineId.c_str(),
        WiFi.macAddress().c_str()
    );

    if (hasCredentials) {
        connected = _client.connect(
            clientId.c_str(),
            AppSecrets::MqttConfig::USERNAME,
            AppSecrets::MqttConfig::PASSWORD,
            statusTopic.c_str(),
            1,
            true,
            willPayload
        );
    } else {
        connected = _client.connect(
            clientId.c_str(),
            statusTopic.c_str(),
            1,
            true,
            willPayload
        );
    }

    if (connected) {
        handleConnected();
    } else {
        Serial.print("[MqttManager][ERREUR] Connexion échouée. Code : ");
        Serial.println(_client.state());

        _state = State::DISCONNECTED;
    }
}

void MqttManager::handleConnected() {
    _state = State::CONNECTED;

    Serial.println("----------------------------------------------");
    Serial.println("[MqttManager] MQTT connecté.");

    Serial.print("[MqttManager] Client ID : ");
    Serial.println(machineIdentity.getId());

    Serial.println("----------------------------------------------");

    subscribeToTopics();

    const String machineId = machineIdentity.getId();
    const String statusTopic = machineIdentity.topicStatus();

    char onlinePayload[256];
    snprintf(
        onlinePayload,
        sizeof(onlinePayload),
        "{\"type\":\"mqtt_status\",\"status\":\"ONLINE\",\"isOnline\":true,\"machineId\":\"%s\",\"machineCode\":\"%s\",\"mac\":\"%s\"}",
        machineId.c_str(),
        machineId.c_str(),
        WiFi.macAddress().c_str()
    );

    publish(statusTopic.c_str(), onlinePayload, true);
}

void MqttManager::handleDisconnected() {
    _state = State::DISCONNECTED;

    Serial.println("----------------------------------------------");
    Serial.println("[MqttManager][WARN] Connexion MQTT perdue.");
    Serial.println("[MqttManager] Reconnexion automatique prévue.");
    Serial.println("----------------------------------------------");
}

void MqttManager::subscribeToTopics() {
    if (!_client.connected()) {
        return;
    }

    // ✅ Topic de commande dynamique
    const String cmdTopic = machineIdentity.topicCommands();

    if (_client.subscribe(cmdTopic.c_str())) {
        Serial.print("[MqttManager] Abonné au topic : ");
        Serial.println(cmdTopic);
    } else {
        Serial.print("[MqttManager][ERREUR] Abonnement impossible : ");
        Serial.println(cmdTopic);
    }
}

void MqttManager::mqttCallbackStatic(
    char* topic,
    byte* payload,
    unsigned int length
) {
    if (_instance != nullptr) {
        _instance->handleIncomingMessage(topic, payload, length);
    }
}

void MqttManager::handleIncomingMessage(
    char* topic,
    byte* payload,
    unsigned int length
) {
    if (length >= AppConfig::Limits::MQTT_PAYLOAD_MAX_SIZE) {
        Serial.println("[MqttManager][WARN] Message MQTT trop long, ignoré.");
        return;
    }

    char message[AppConfig::Limits::MQTT_PAYLOAD_MAX_SIZE];
    memcpy(message, payload, length);
    message[length] = '\0';

    Serial.println("----------------------------------------------");
    Serial.println("[MqttManager] Message MQTT reçu.");
    Serial.print("[MqttManager] Topic : ");
    Serial.println(topic);
    Serial.print("[MqttManager] Payload : ");
    Serial.println(message);
    Serial.println("----------------------------------------------");

    if (_messageCallback != nullptr) {
        _messageCallback(topic, message);
    }
}

const char* MqttManager::stateToString(State state) const {
    switch (state) {
        case State::DISCONNECTED: return "DISCONNECTED";
        case State::CONNECTING:   return "CONNECTING";
        case State::CONNECTED:    return "CONNECTED";
        default:                  return "UNKNOWN";
    }
}