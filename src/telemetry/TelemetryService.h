// src/telemetry/TelemetryService.h

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "../config/config.h"
#include "../config/MachineIdentity.h"

class TelemetryService {
public:
    using PublishCallback = bool (*)(const char* topic, const char* payload);

    TelemetryService();

    void begin();
    void setPublisher(PublishCallback publisher);

    bool publishBoot();
    bool publishHeartbeat(AppConfig::SystemState state);

    bool publishMachineAvailabilityStatus(bool machineCanAcceptPayment);

    bool publishSystemEvent(
        const char* eventType,
        const char* message
    );

    bool publishTransactionEvent(
        const char* eventType,
        const char* transactionId,
        uint16_t amountFcfa,
        const char* source,
        const char* status
    );

    bool publishCoinPaymentEvent(
        uint16_t amountFcfa,
        uint16_t pulseCount,
        const char* source,
        const char* eventId = nullptr
    );

    bool publishError(
        const char* errorCode,
        const char* message
    );

private:
    PublishCallback _publisher;

    bool publishJson(
        const char* topic,
        JsonDocument& doc
    );

    const char* systemStateToString(AppConfig::SystemState state) const;
};