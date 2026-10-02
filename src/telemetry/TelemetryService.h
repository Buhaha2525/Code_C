// src/telemetry/TelemetryService.h

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "../config/config.h"
#include "../config/MachineIdentity.h"

/*
   =========================================================
   MODULE : TelemetryService
   Rôle :
   - Générer des messages JSON propres pour Grafana
   - Publier les états système, heartbeats, transactions
   - Publier les paiements par type (coin / wave / orange_money)
   =========================================================
*/

class TelemetryService {
public:
    using PublishCallback = bool (*)(const char* topic, const char* payload);

    TelemetryService();

    void begin();
    void setPublisher(PublishCallback publisher);

    // ==== ÉTAT SYSTÈME ====
    bool publishBoot();
    bool publishHeartbeat(AppConfig::SystemState state);
    bool publishMachineAvailabilityStatus(bool machineCanAcceptPayment);

    // ==== ÉVÉNEMENTS ====
    bool publishSystemEvent(const char* eventType, const char* message);

    // ==== TRANSACTIONS (améliorée avec paymentMethod) ====
    bool publishTransactionEvent(
        const char* eventType,
        const char* transactionId,
        uint16_t amountFcfa,
        const char* source,
        const char* status,
        const char* paymentMethod = "coin"
    );

    // ==== PAIEMENT DÉDIÉ (pour Grafana) ====
    bool publishPaymentEvent(
        const char* paymentMethod,
        uint16_t amountFcfa,
        const char* transactionId,
        const char* status,
        const char* source
    );

    // ==== COIN PHYSIQUE ====
    bool publishCoinPaymentEvent(
        uint16_t amountFcfa,
        uint16_t pulseCount,
        const char* source,
        const char* eventId = nullptr
    );

    // ==== ERREURS ====
    bool publishError(const char* errorCode, const char* message);

private:
    PublishCallback _publisher;

    bool publishJson(const char* topic, JsonDocument& doc);

    const char* systemStateToString(AppConfig::SystemState state) const;
};