// src/telemetry/TelemetryService.cpp

#include "TelemetryService.h"
#include <WiFi.h>

TelemetryService::TelemetryService()
    : _publisher(nullptr) {
}

void TelemetryService::begin() {
    Serial.println("[TelemetryService] Module initialisé.");
}

void TelemetryService::setPublisher(PublishCallback publisher) {
    _publisher = publisher;
}

bool TelemetryService::publishBoot() {
    JsonDocument doc;

    doc["type"] = "boot";
    doc["status"] = "ONLINE";
    doc["isOnline"] = true;
    doc["machineId"] = machineIdentity.getId();
    doc["machineCode"] = machineIdentity.getId();
    doc["firmwareVersion"] = AppConfig::Machine::FIRMWARE_VERSION;
    doc["uptimeMs"] = millis();

    return publishJson(machineIdentity.topicStatus().c_str(), doc);
}

bool TelemetryService::publishHeartbeat(AppConfig::SystemState state) {
    JsonDocument doc;

    doc["type"] = "heartbeat";
    doc["status"] = "ONLINE";
    doc["isOnline"] = true;
    doc["machineId"] = machineIdentity.getId();
    doc["machineCode"] = machineIdentity.getId();
    doc["firmwareVersion"] = AppConfig::Machine::FIRMWARE_VERSION;
    doc["state"] = systemStateToString(state);
    doc["uptimeMs"] = millis();
    doc["freeHeap"] = ESP.getFreeHeap();

    return publishJson(machineIdentity.topicTelemetry().c_str(), doc);
}

bool TelemetryService::publishMachineAvailabilityStatus(
    bool machineCanAcceptPayment
) {
    JsonDocument doc;

    doc["type"] = "machine_status";
    doc["status"] = "ONLINE";
    doc["isOnline"] = true;
    doc["machineId"] = machineIdentity.getId();
    doc["machineCode"] = machineIdentity.getId();
    doc["firmwareVersion"] = AppConfig::Machine::FIRMWARE_VERSION;
    doc["machineCanAcceptPayment"] = machineCanAcceptPayment;
    doc["isAvailable"] = machineCanAcceptPayment;
    doc["canDispense"] = machineCanAcceptPayment;
    doc["counterLevel"] = machineCanAcceptPayment ? "HIGH" : "LOW";
    doc["uptimeMs"] = millis();

    if (!machineCanAcceptPayment) {
        doc["reason"] = "MACHINE_UNAVAILABLE_OR_STOP_SALE";
    }

    return publishJson(machineIdentity.topicStatus().c_str(), doc);
}

bool TelemetryService::publishSystemEvent(
    const char* eventType,
    const char* message
) {
    JsonDocument doc;

    doc["type"] = "system_event";
    doc["eventType"] = eventType;
    doc["machineId"] = machineIdentity.getId();
    doc["message"] = message;
    doc["uptimeMs"] = millis();

    return publishJson(machineIdentity.topicEvents().c_str(), doc);
}

bool TelemetryService::publishTransactionEvent(
    const char* eventType,
    const char* transactionId,
    uint16_t amountFcfa,
    const char* source,
    const char* status
) {
    JsonDocument doc;

    doc["type"] = "transaction_event";
    doc["eventType"] = eventType;
    doc["machineId"] = machineIdentity.getId();
    doc["machineCode"] = machineIdentity.getId();
    doc["transactionId"] = transactionId;
    doc["eventId"] = transactionId;
    doc["amount"] = amountFcfa;
    doc["amountFcfa"] = amountFcfa;
    doc["source"] = source;
    doc["status"] = status;
    doc["uptimeMs"] = millis();

    return publishJson(machineIdentity.topicEvents().c_str(), doc);
}

bool TelemetryService::publishError(
    const char* errorCode,
    const char* message
) {
    JsonDocument doc;

    doc["type"] = "error";
    doc["machineId"] = machineIdentity.getId();
    doc["errorCode"] = errorCode;
    doc["message"] = message;
    doc["uptimeMs"] = millis();

    return publishJson(machineIdentity.topicEvents().c_str(), doc);
}

bool TelemetryService::publishJson(
    const char* topic,
    JsonDocument& doc
) {
    doc["macAddress"] = WiFi.macAddress();

    char payload[AppConfig::Limits::TELEMETRY_JSON_SIZE];

    const size_t length = serializeJson(doc, payload, sizeof(payload));

    if (length == 0) {
        Serial.println("[TelemetryService][ERREUR] JSON vide.");
        return false;
    }

    if (length >= sizeof(payload)) {
        Serial.println("[TelemetryService][ERREUR] Payload JSON trop grand.");
        return false;
    }

    if (_publisher != nullptr) {
        const bool published = _publisher(topic, payload);

        if (published) {
            return true;
        }

        Serial.println("[TelemetryService][WARN] Publication MQTT refusée.");
        return false;
    }

    Serial.println("----------------------------------------------");
    Serial.print("[TelemetryService][LOCAL] Topic : ");
    Serial.println(topic);
    Serial.print("[TelemetryService][LOCAL] Payload : ");
    Serial.println(payload);
    Serial.println("----------------------------------------------");

    return true;
}

bool TelemetryService::publishCoinPaymentEvent(
    uint16_t amountFcfa,
    uint16_t pulseCount,
    const char* source,
    const char* eventId
) {
    JsonDocument doc;

    doc["type"] = "coin_payment_detected";
    doc["eventType"] = "physical_coin_payment";
    doc["machineId"] = machineIdentity.getId();
    doc["amountFcfa"] = amountFcfa;
    doc["pulseCount"] = pulseCount;
    doc["source"] = (source != nullptr && strlen(source) > 0) ? source : "physical_coin";
    doc["status"] = "DETECTED";
    doc["uptimeMs"] = millis();

    if (eventId != nullptr) {
        doc["eventId"] = eventId;
        doc["transactionId"] = eventId;
    }

    return publishJson(machineIdentity.topicEvents().c_str(), doc);
}

const char* TelemetryService::systemStateToString(
    AppConfig::SystemState state
) const {
    switch (state) {
        case AppConfig::SystemState::BOOT:              return "BOOT";
        case AppConfig::SystemState::WIFI_CONNECTING:   return "WIFI_CONNECTING";
        case AppConfig::SystemState::MQTT_CONNECTING:   return "MQTT_CONNECTING";
        case AppConfig::SystemState::IDLE:              return "IDLE";
        case AppConfig::SystemState::WAITING_FOR_COIN:  return "WAITING_FOR_COIN";
        case AppConfig::SystemState::COIN_RECEIVED:     return "COIN_RECEIVED";
        case AppConfig::SystemState::VALIDATING_COIN:   return "VALIDATING_COIN";
        case AppConfig::SystemState::DISPENSING:        return "DISPENSING";
        case AppConfig::SystemState::DISPENSE_SUCCESS:  return "DISPENSE_SUCCESS";
        case AppConfig::SystemState::DISPENSE_FAILED:   return "DISPENSE_FAILED";
        case AppConfig::SystemState::ERROR:             return "ERROR";
        case AppConfig::SystemState::MAINTENANCE:       return "MAINTENANCE";
        default:                                        return "UNKNOWN";
    }
}