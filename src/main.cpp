// src/main.cpp

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>

#include "config/config.h"
#include "config/secrets.h"
#include "config/MachineIdentity.h"
#include "hardware/PulseOutput.h"
#include "hardware/CoinAcceptor.h"
#include "hardware/Dispenser.h"
#include "transaction/TransactionManager.h"
#include "transaction/TransactionStore.h"
#include "telemetry/TelemetryService.h"
#include "security/CommandValidator.h"
#include "network/WiFiManager.h"
#include "network/MqttManager.h"
#include "telemetry/OtaService.h"

using AppConfig::SystemState;

static SystemState currentState = SystemState::BOOT;
static uint32_t lastHeartbeatMs = 0;
static bool ledState = false;

PulseOutput pulseOutput;
CoinAcceptor coinAcceptor;
Dispenser dispenser;
TransactionManager transactionManager;
TransactionStore transactionStore;
TelemetryService telemetryService;
CommandValidator commandValidator;
WiFiManager wifiManager;
MqttManager mqttManager;
OtaService otaService;

bool coinInputDisabledBySystem = false;

uint32_t lastRawDebugMs = 0;
String lastProcessedMqttTransactionId = "";
uint32_t lastProcessedMqttTransactionMs = 0;

uint16_t lastCoinEventAmount = 0;
uint16_t lastCoinEventPulses = 0;
uint32_t lastCoinEventMs = 0;

bool lastPublishedMachineAvailability = false;
bool hasPublishedMachineAvailability = false;
uint32_t lastMachineAvailabilityPublishMs = 0;

static constexpr uint32_t COIN_EVENT_DUPLICATE_WINDOW_MS = 1000;
static constexpr uint32_t MACHINE_AVAILABILITY_PERIODIC_PUBLISH_MS = 5000;

void disableCoinInputDuringCreditEmission();
void enableCoinInputAfterCreditEmission();

const char* systemStateToString(SystemState state) {
    switch (state) {
        case SystemState::BOOT: return "BOOT";
        case SystemState::WIFI_CONNECTING: return "WIFI_CONNECTING";
        case SystemState::MQTT_CONNECTING: return "MQTT_CONNECTING";
        case SystemState::IDLE: return "IDLE";
        case SystemState::WAITING_FOR_COIN: return "WAITING_FOR_COIN";
        case SystemState::COIN_RECEIVED: return "COIN_RECEIVED";
        case SystemState::VALIDATING_COIN: return "VALIDATING_COIN";
        case SystemState::DISPENSING: return "DISPENSING";
        case SystemState::DISPENSE_SUCCESS: return "DISPENSE_SUCCESS";
        case SystemState::DISPENSE_FAILED: return "DISPENSE_FAILED";
        case SystemState::ERROR: return "ERROR";
        case SystemState::MAINTENANCE: return "MAINTENANCE";
        default: return "UNKNOWN";
    }
}

void setSystemState(SystemState newState) {
    currentState = newState;
    Serial.print("[STATE] Nouvel état système : ");
    Serial.println(systemStateToString(currentState));
}

bool isDuplicateMqttTransactionId(const char* transactionId);
void rememberMqttTransactionId(const char* transactionId);
bool isMachineCanAcceptPayment();
void updateMachineAvailabilityStatus(bool forcePublish = false);
bool isDuplicateCoinPaymentEvent(uint16_t amountFcfa, uint16_t pulseCount);
void rememberCoinPaymentEvent(uint16_t amountFcfa, uint16_t pulseCount);
String buildCoinPaymentEventId(uint16_t amountFcfa, uint16_t pulseCount);

void initHardwarePins() {
    pinMode(AppConfig::Pins::COIN_INPUT_PIN, INPUT_PULLUP);
    pinMode(AppConfig::Pins::LED_STATUS_PIN, OUTPUT);
    pinMode(AppConfig::Pins::PULSE_OUT_PIN, OUTPUT);
    digitalWrite(AppConfig::Pins::LED_STATUS_PIN, LOW);
    digitalWrite(AppConfig::Pins::PULSE_OUT_PIN, LOW);
    Serial.println("[BOOT] Pins hardware initialisées.");
}

void printBootInfo() {
    Serial.println();
    Serial.println("==============================================");
    Serial.println("   MONNAYEUR / DISTRIBUTEUR INTELLIGENT ESP32 ");
    Serial.println("==============================================");

    Serial.print("Machine ID       : ");
    Serial.println(machineIdentity.getId());

    Serial.print("Firmware version : ");
    Serial.println(AppConfig::Machine::FIRMWARE_VERSION);

    Serial.println("----------------------------------------------");
    Serial.println("Topics MQTT dynamiques :");
    Serial.print("Commands  : ");
    Serial.println(machineIdentity.topicCommands());
    Serial.print("Events    : ");
    Serial.println(machineIdentity.topicEvents());
    Serial.print("Telemetry : ");
    Serial.println(machineIdentity.topicTelemetry());
    Serial.print("Status    : ");
    Serial.println(machineIdentity.topicStatus());
    Serial.print("ACKs      : ");
    Serial.println(machineIdentity.topicAcks());

    Serial.println("----------------------------------------------");
    Serial.println("Configuration hardware :");
    Serial.print("Coin input pin   : GPIO ");
    Serial.println(AppConfig::Pins::COIN_INPUT_PIN);
    Serial.print("LED status pin   : GPIO ");
    Serial.println(AppConfig::Pins::LED_STATUS_PIN);
    Serial.print("Pulse output pin : GPIO ");
    Serial.println(AppConfig::Pins::PULSE_OUT_PIN);

    Serial.println("----------------------------------------------");
    Serial.println("Tarifs configurés :");
    for (uint8_t i = 0; i < AppConfig::TARIFF_COUNT; i++) {
        Serial.print("- ");
        Serial.print(AppConfig::TARIFFS[i].label);
        Serial.print(" = ");
        Serial.print(AppConfig::TARIFFS[i].pulses);
        Serial.println(" impulsions");
    }

    Serial.println("----------------------------------------------");
    Serial.print("ESP MAC (STA)    : ");
    Serial.println(WiFi.macAddress());
    Serial.print("ESP MAC (SoftAP) : ");
    Serial.println(WiFi.softAPmacAddress());
    Serial.println("==============================================");
    Serial.println();
}

void updateHeartbeat() {
    const uint32_t now = millis();
    if (now - lastHeartbeatMs >= AppConfig::Timing::HEARTBEAT_INTERVAL_MS) {
        lastHeartbeatMs = now;
        ledState = !ledState;
        digitalWrite(AppConfig::Pins::LED_STATUS_PIN, ledState ? HIGH : LOW);

        Serial.print("[HEARTBEAT] Système vivant | État : ");
        Serial.println(systemStateToString(currentState));

        telemetryService.publishHeartbeat(currentState);

        const bool machineCanAcceptPayment = isMachineCanAcceptPayment();
        telemetryService.publishMachineAvailabilityStatus(machineCanAcceptPayment);

        Serial.print("[MACHINE][COUNTER] Disponibilité machine : ");
        Serial.println(machineCanAcceptPayment ? "AVAILABLE" : "UNAVAILABLE");
    }
}

void enableCoinInputAfterCreditEmission() {
    coinAcceptor.enable();
    coinInputDisabledBySystem = false;
    Serial.println("[MAIN][PROTECTION] Lecture COIN réactivée.");
}

void handleSerialPulseTest() {
    if (!Serial.available()) return;

    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() == 0) return;

    if (input.equalsIgnoreCase("clear") || input.equalsIgnoreCase("reset")) {
        transactionStore.clear();
        Serial.println("[STORE] Mémoire flash transactionnelle nettoyée !");
        return;
    }

    if (input.equalsIgnoreCase("ota")) {
        Serial.println("[MAIN] Commande OTA manuelle reçue.");
        otaService.checkAndPerformUpdate();
        return;
    }

    // ✅ Afficher l'ID actuel
    if (input.equalsIgnoreCase("id")) {
        Serial.print("[MAIN] ID machine actuel : '");
        Serial.print(machineIdentity.getId());
        Serial.println("'");
        return;
    }

    // ✅ Changer l'ID localement
    if (input.startsWith("setid ")) {
        String newId = input.substring(6);
        newId.trim();

        if (machineIdentity.setId(newId)) {
            Serial.println("[MAIN] ✅ ID changé. Redémarrage dans 2 secondes...");
            delay(2000);
            ESP.restart();
        } else {
            Serial.println("[MAIN] ❌ Échec du changement d'ID.");
        }
        return;
    }

    const AppConfig::Tariff* selectedTariff = nullptr;
    if (input == "1")      selectedTariff = &AppConfig::TARIFFS[0];
    else if (input == "2") selectedTariff = &AppConfig::TARIFFS[1];
    else if (input == "3") selectedTariff = &AppConfig::TARIFFS[2];
    else {
        Serial.println("[TEST] Commandes : 1, 2, 3, 'ota', 'clear', 'id', 'setid <nouvel_id>'");
        return;
    }

    char transactionId[40];
    snprintf(transactionId, sizeof(transactionId), "SERIAL-%lu", millis());

    CommandValidator::DispenseCommand command = {
        "DISPENSE",
        machineIdentity.getId().c_str(),
        transactionId,
        selectedTariff->amountFcfa,
        "serial_test"
    };

    CommandValidator::ValidationResult validation;
    if (!commandValidator.validateDispenseCommand(command, validation)) {
        telemetryService.publishError(validation.errorCode, validation.message);
        setSystemState(SystemState::IDLE);
        return;
    }

    disableCoinInputDuringCreditEmission();
    if (transactionManager.startTransaction(
            selectedTariff->amountFcfa, transactionId, "serial_test")) {
        setSystemState(SystemState::DISPENSING);
    } else {
        enableCoinInputAfterCreditEmission();
    }
}

bool mqttPublishAdapter(const char* topic, const char* payload) {
    return mqttManager.publish(topic, payload);
}

bool syncPendingTransactionsToMqtt(const char* topic, const char* payload) {
    return mqttManager.publish(topic, payload);
}

void handleMqttMessage(const char* topic, const char* payload) {
    Serial.println("----------------------------------------------");
    Serial.print("[MAIN] Topic : ");
    Serial.println(topic);
    Serial.print("[MAIN] Payload : ");
    Serial.println(payload);
    Serial.println("----------------------------------------------");

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        telemetryService.publishError("INVALID_JSON", "Payload MQTT JSON invalide.");
        return;
    }

    const char* action = doc["action"] | "";
    const char* machineId = doc["machineId"] | "";
    const char* transactionId = doc["transactionId"] | "";
    const char* source = doc["source"] | "mqtt";
    uint16_t amountFcfa = doc["amountFcfa"] | 0;

    // ===== COMMANDES ADMIN =====
    if (strcmp(action, "OTA_UPDATE") == 0) {
        Serial.println("[MAIN][MQTT][ADMIN] Commande OTA reçue.");
        otaService.checkAndPerformUpdate();
        return;
    }

    if (strcmp(action, "REBOOT") == 0) {
        Serial.println("[MAIN][MQTT][ADMIN] Commande REBOOT reçue.");
        delay(500);
        ESP.restart();
        return;
    }

    // ✅ Changement d'ID à distance
    if (strcmp(action, "SET_ID") == 0) {
        const char* newId = doc["newId"] | "";
        Serial.print("[MAIN][MQTT][ADMIN] Commande SET_ID reçue : '");
        Serial.print(newId);
        Serial.println("'");

        if (machineIdentity.setId(String(newId))) {
            telemetryService.publishError("ID_CHANGED", "Redémarrage imminent.");
            delay(1000);
            ESP.restart();
        } else {
            telemetryService.publishError("ID_CHANGE_FAILED", "ID invalide.");
        }
        return;
    }

    // ===== DISPENSE =====
    CommandValidator::DispenseCommand command = {
        action, machineId, transactionId, amountFcfa, source
    };

    CommandValidator::ValidationResult validation;
    if (!commandValidator.validateDispenseCommand(command, validation)) {
        telemetryService.publishError(validation.errorCode, validation.message);
        return;
    }

    if (isDuplicateMqttTransactionId(transactionId)) {
        telemetryService.publishError("DUPLICATE_MQTT_TRANSACTION",
                                      "Transaction déjà traitée.");
        return;
    }

    const bool machineCanAcceptPayment = isMachineCanAcceptPayment();
    telemetryService.publishMachineAvailabilityStatus(machineCanAcceptPayment);

    if (!machineCanAcceptPayment) {
        rememberMqttTransactionId(transactionId);
        telemetryService.publishError("MACHINE_UNAVAILABLE_COUNTER_LOW",
                                      "Machine indisponible.");
        return;
    }

    rememberMqttTransactionId(transactionId);
    disableCoinInputDuringCreditEmission();

    if (!transactionManager.startTransaction(amountFcfa, transactionId, source)) {
        telemetryService.publishError("TRANSACTION_START_FAILED",
                                      "Impossible de démarrer la transaction.");
        enableCoinInputAfterCreditEmission();
        return;
    }

    setSystemState(SystemState::DISPENSING);
}

void debugRawCoinInputPeriodic() {
    if (!AppConfig::Debug::COIN_INPUT_RAW_LOG_ENABLED) return;
    const uint32_t now = millis();
    if (now - lastRawDebugMs < AppConfig::Debug::COIN_INPUT_RAW_LOG_INTERVAL_MS) return;
    lastRawDebugMs = now;
    const int level = digitalRead(AppConfig::Pins::COIN_INPUT_PIN);
    Serial.print("[DEBUG][GPIO27] Niveau = ");
    Serial.println(level == HIGH ? "HIGH" : "LOW");
}

bool isDuplicateMqttTransactionId(const char* transactionId) {
    if (transactionId == nullptr) return false;
    String id = String(transactionId);
    if (id.length() == 0) return false;
    return id == lastProcessedMqttTransactionId;
}

void rememberMqttTransactionId(const char* transactionId) {
    if (transactionId == nullptr) return;
    lastProcessedMqttTransactionId = String(transactionId);
    lastProcessedMqttTransactionMs = millis();
}

bool isDuplicateCoinPaymentEvent(uint16_t amountFcfa, uint16_t pulseCount) {
    const uint32_t now = millis();
    if (lastCoinEventMs == 0) return false;
    return (amountFcfa == lastCoinEventAmount &&
            pulseCount == lastCoinEventPulses &&
            (now - lastCoinEventMs) <= COIN_EVENT_DUPLICATE_WINDOW_MS);
}

void rememberCoinPaymentEvent(uint16_t amountFcfa, uint16_t pulseCount) {
    lastCoinEventAmount = amountFcfa;
    lastCoinEventPulses = pulseCount;
    lastCoinEventMs = millis();
}

String buildCoinPaymentEventId(uint16_t amountFcfa, uint16_t pulseCount) {
    String eventId = machineIdentity.getId();
    eventId += "-COIN-";
    eventId += String(millis());
    eventId += "-";
    eventId += String(amountFcfa);
    eventId += "-";
    eventId += String(pulseCount);
    return eventId;
}

void disableCoinInputDuringCreditEmission() {
    coinInputDisabledBySystem = true;
    coinAcceptor.disable();
    Serial.println("[MAIN][PROTECTION] Lecture COIN désactivée.");
}

bool isMachineCanAcceptPayment() {
    static int lastPinState = HIGH;
    static int debouncedState = HIGH;
    static uint32_t lastDebounceTimeMs = 0;
    static constexpr uint32_t DEBOUNCE_DELAY_MS = 100;

    const int reading = digitalRead(AppConfig::Pins::MACHINE_AVAILABLE_PIN);
    const uint32_t now = millis();

    if (reading != lastPinState) {
        lastDebounceTimeMs = now;
        lastPinState = reading;
    }

    if ((now - lastDebounceTimeMs) >= DEBOUNCE_DELAY_MS) {
        debouncedState = reading;
    }

    return debouncedState == HIGH;
}

void updateMachineAvailabilityStatus(bool forcePublish) {
    const bool machineCanAcceptPayment = isMachineCanAcceptPayment();
    const uint32_t now = millis();
    const bool changed = !hasPublishedMachineAvailability || machineCanAcceptPayment != lastPublishedMachineAvailability;
    const bool periodic = now - lastMachineAvailabilityPublishMs >= MACHINE_AVAILABILITY_PERIODIC_PUBLISH_MS;

    if (!forcePublish && !changed && !periodic) return;

    telemetryService.publishMachineAvailabilityStatus(machineCanAcceptPayment);

    lastPublishedMachineAvailability = machineCanAcceptPayment;
    hasPublishedMachineAvailability = true;
    lastMachineAvailabilityPublishMs = now;

    Serial.print("[MACHINE][COUNTER] Status publié : ");
    Serial.println(machineCanAcceptPayment ? "AVAILABLE" : "UNAVAILABLE");
}

void setup() {
    Serial.begin(115200);
    delay(500);

    // ✅ Initialiser l'identité machine AVANT tout le reste
    machineIdentity.begin();

    printBootInfo();
    initHardwarePins();

    pinMode(AppConfig::Pins::COIN_INPUT_PIN, INPUT_PULLUP);
    pinMode(AppConfig::Pins::MACHINE_AVAILABLE_PIN, INPUT);

    pulseOutput.begin();
    coinAcceptor.begin();
    dispenser.begin(&pulseOutput);

    transactionStore.begin();

    if (transactionStore.hasPendingTransaction()) {
        Serial.println("[BOOT][WARN] Une transaction locale semble avoir été interrompue.");
        transactionStore.printLastTransaction();
    }

    transactionManager.begin(&dispenser, &transactionStore);

    telemetryService.begin();
    commandValidator.begin();

    wifiManager.begin();

    mqttManager.begin();
    mqttManager.setMessageCallback(handleMqttMessage);
    telemetryService.setPublisher(mqttPublishAdapter);

    telemetryService.publishBoot();

    setSystemState(SystemState::IDLE);

    Serial.println("[BOOT] Initialisation terminée.");
}

void loop() {
    wifiManager.update();
    mqttManager.update();

    if (mqttManager.isConnected() && transactionStore.hasPendingSync()) {
        transactionStore.syncPendingTransactions(
            syncPendingTransactionsToMqtt,
            machineIdentity.topicEvents().c_str()   // ✅ topic dynamique
        );
    }

    updateMachineAvailabilityStatus(false);
    debugRawCoinInputPeriodic();
    updateHeartbeat();
    handleSerialPulseTest();

    // ✅ OTA automatique
    otaService.update();

    // 💧 Protection eau : désactivation du pin 27 pendant le puisage ou l'émission
    const bool machineBusyOrDispensing = !isMachineCanAcceptPayment() || transactionManager.isBusy();
    if (machineBusyOrDispensing && !coinInputDisabledBySystem) {
        disableCoinInputDuringCreditEmission();
    } else if (!machineBusyOrDispensing && coinInputDisabledBySystem && !transactionManager.isBusy()) {
        enableCoinInputAfterCreditEmission();
    }

    if (!coinInputDisabledBySystem) {
        coinAcceptor.update();

        if (coinAcceptor.hasCoinEvent()) {
            const uint16_t amount = coinAcceptor.getLastAmountFcfa();
            const uint16_t pulses = coinAcceptor.getLastPulseCount();

            Serial.println("----------------------------------------------");
            Serial.println("[MAIN] Événement monnayeur détecté.");
            Serial.print("[MAIN] Impulsions reçues : ");
            Serial.println(pulses);

            if (amount > 0) {
                Serial.print("[MAIN] Montant reconnu : ");
                Serial.print(amount);
                Serial.println(" FCFA.");

                if (isDuplicateCoinPaymentEvent(amount, pulses)) {
                    Serial.println("[MAIN][ANTI-DOUBLON] Paiement déjà publié récemment.");
                } else {
                    String eventId = buildCoinPaymentEventId(amount, pulses);

                    const bool published = telemetryService.publishCoinPaymentEvent(
                        amount, pulses, "physical_coin", eventId.c_str()
                    );

                    if (published) {
                        Serial.print("[MAIN] Paiement publié. Event ID : ");
                        Serial.println(eventId);
                    } else {
                        transactionStore.enqueueOfflineTransaction(
                            eventId.c_str(), amount, "physical_coin",
                            TransactionStore::Status::CREATED, pulses
                        );
                    }
                }
                setSystemState(SystemState::IDLE);
            }
            Serial.println("----------------------------------------------");
        }
    }

    transactionManager.update();

    if (transactionManager.hasSucceeded()) {
        telemetryService.publishTransactionEvent(
            "transaction_success",
            transactionManager.getLastCompletedTransactionId(),
            transactionManager.getLastCompletedAmountFcfa(),
            transactionManager.getLastCompletedSource(),
            "SUCCESS"
        );
        if (isMachineCanAcceptPayment()) {
            enableCoinInputAfterCreditEmission();
        }
        setSystemState(AppConfig::SystemState::IDLE);
    }

    if (transactionManager.hasFailed()) {
        telemetryService.publishTransactionEvent(
            "transaction_failed",
            transactionManager.getCurrentTransactionId(),
            transactionManager.getCurrentAmountFcfa(),
            transactionManager.getCurrentSource(),
            "FAILED"
        );
        if (isMachineCanAcceptPayment()) {
            enableCoinInputAfterCreditEmission();
        }
        setSystemState(SystemState::IDLE);
    }
}