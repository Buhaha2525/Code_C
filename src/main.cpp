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
#include "telemetry/LogService.h"
#include "security/CommandValidator.h"
#include "network/WiFiManager.h"
#include "network/MqttManager.h"
#include "telemetry/OtaService.h"

/*
   =========================================================
   PROJET : MONNAYEUR / DISTRIBUTEUR INTELLIGENT CONNECTÉ
   CARTE  : ESP32 DOIT DEVKIT V1
   =========================================================
*/

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

// =====================================================
// PROTOTYPES
// =====================================================
void disableCoinInputDuringCreditEmission();
void enableCoinInputAfterCreditEmission();

const char* systemStateToString(SystemState state);
void setSystemState(SystemState newState);

bool isDuplicateMqttTransactionId(const char* transactionId);
void rememberMqttTransactionId(const char* transactionId);

bool isMachineCanAcceptPayment();
void updateMachineAvailabilityStatus(bool forcePublish = false);

bool isDuplicateCoinPaymentEvent(uint16_t amountFcfa, uint16_t pulseCount);
void rememberCoinPaymentEvent(uint16_t amountFcfa, uint16_t pulseCount);
String buildCoinPaymentEventId(uint16_t amountFcfa, uint16_t pulseCount);

const char* detectPaymentMethod(const char* source);
const char* paymentMethodToLabel(const char* method);

void printSecurityInfo();


// =====================================================
// HELPERS
// =====================================================

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

    remoteLog.info("STATE", systemStateToString(currentState));
}

const char* detectPaymentMethod(const char* source) {
    if (source == nullptr) return "coin";

    String s = String(source);
    s.toLowerCase();

    if (s.indexOf("wave") >= 0) return "wave";
    if (s.indexOf("orange") >= 0) return "orange_money";
    if (s.indexOf("om") >= 0) return "orange_money";
    if (s.indexOf("coin") >= 0) return "coin";
    if (s.indexOf("physical") >= 0) return "coin";

    return "coin";
}

const char* paymentMethodToLabel(const char* method) {
    if (method == nullptr) return "Inconnu";
    if (strcmp(method, "coin") == 0) return "PIECE";
    if (strcmp(method, "wave") == 0) return "WAVE";
    if (strcmp(method, "orange_money") == 0) return "ORANGE_MONEY";
    return "INCONNU";
}


// =====================================================
// SECURITY INFO (nouvelle fonction utilitaire)
// =====================================================

void printSecurityInfo() {
    Serial.println("==============================================");
    Serial.println("  ÉTAT DE SÉCURITÉ");
    Serial.println("==============================================");

#ifdef CONFIG_SECURE_BOOT
    Serial.println("Secure Boot       : ✅ ACTIF");
#else
    Serial.println("Secure Boot       : ❌ INACTIF");
#endif

#ifdef CONFIG_SECURE_FLASH_ENC_ENABLED
    Serial.println("Flash Encryption  : ✅ ACTIF");
#else
    Serial.println("Flash Encryption  : ❌ INACTIF");
#endif

#ifdef CONFIG_SECURE_BOOT_V2_ENABLED
    Serial.println("Secure Boot V2    : ✅ ACTIF");
#else
    Serial.println("Secure Boot V2    : ❌ INACTIF");
#endif

    Serial.print("Firmware version  : ");
    Serial.println(AppConfig::Machine::FIRMWARE_VERSION);

    Serial.println("==============================================");
}


// =====================================================
// INITIALISATION HARDWARE
// =====================================================

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

    Serial.print("Logs      : ");
    Serial.println(machineIdentity.topicLogs());

    Serial.println("----------------------------------------------");
    Serial.println("Configuration hardware :");

    Serial.print("Coin input pin   : GPIO ");
    Serial.println(AppConfig::Pins::COIN_INPUT_PIN);

    Serial.print("LED status pin   : GPIO ");
    Serial.println(AppConfig::Pins::LED_STATUS_PIN);

    Serial.print("Pulse output pin : GPIO ");
    Serial.println(AppConfig::Pins::PULSE_OUT_PIN);

    Serial.print("Machine avail pin: GPIO ");
    Serial.println(AppConfig::Pins::MACHINE_AVAILABLE_PIN);

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

    printSecurityInfo();

    Serial.println();
}


// =====================================================
// HEARTBEAT
// =====================================================

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
        Serial.print(machineCanAcceptPayment ? "AVAILABLE" : "UNAVAILABLE");

        const int rawPin = digitalRead(AppConfig::Pins::MACHINE_AVAILABLE_PIN);
        Serial.print(" (GPIO");
        Serial.print(AppConfig::Pins::MACHINE_AVAILABLE_PIN);
        Serial.print(" = ");
        Serial.print(rawPin == HIGH ? "HIGH" : "LOW");
        Serial.println(")");
    }
}


// =====================================================
// GESTION COIN
// =====================================================

void enableCoinInputAfterCreditEmission() {
    coinAcceptor.enable();
    coinInputDisabledBySystem = false;
    Serial.println("[MAIN][PROTECTION] Lecture COIN réactivée après émission ESP32.");
}

void disableCoinInputDuringCreditEmission() {
    coinInputDisabledBySystem = true;
    coinAcceptor.disable();
    Serial.println("[MAIN][PROTECTION] Lecture COIN désactivée pendant émission ESP32.");
}

bool isDuplicateCoinPaymentEvent(uint16_t amountFcfa, uint16_t pulseCount) {
    const uint32_t now = millis();
    if (lastCoinEventMs == 0) return false;

    return (
        amountFcfa == lastCoinEventAmount &&
        pulseCount == lastCoinEventPulses &&
        (now - lastCoinEventMs) <= COIN_EVENT_DUPLICATE_WINDOW_MS
    );
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


// =====================================================
// COMMANDES SÉRIE (debug)
// =====================================================

void printSerialHelp() {
    Serial.println("[TEST] Commandes disponibles :");
    Serial.println("----------------------------------------------");
    Serial.println("  DISTRIBUTION :");
    Serial.println("    1, 2, 3        → Test pièce (coin) 50/100/200 FCFA");
    Serial.println("    w1, w2         → Test Wave 50/100 FCFA");
    Serial.println("    o1, o2         → Test Orange Money 50/100 FCFA");
    Serial.println("----------------------------------------------");
    Serial.println("  ADMIN :");
    Serial.println("    id                          → Afficher ID machine");
    Serial.println("    setid <pwd> <id>            → Changer ID (protégé)");
    Serial.println("    info                        → Infos système + sécurité");
    Serial.println("    ota                         → Forcer check OTA");
    Serial.println("    clear                       → Vider mémoire transactionnelle");
    Serial.println("----------------------------------------------");
}

void handleSerialPulseTest() {
    if (!Serial.available()) return;

    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() == 0) return;

    // =====================================================
    // COMMANDE : clear / reset
    // =====================================================
    if (input.equalsIgnoreCase("clear") || input.equalsIgnoreCase("reset")) {
        transactionStore.clear();
        Serial.println("[STORE] Mémoire flash transactionnelle nettoyée !");
        remoteLog.info("STORE", "Memoire flash nettoyee");
        return;
    }

    // =====================================================
    // COMMANDE : ota
    // =====================================================
    if (input.equalsIgnoreCase("ota")) {
        Serial.println("[MAIN] Commande OTA manuelle reçue.");
        remoteLog.info("OTA", "Check manuel demande");
        otaService.checkAndPerformUpdate();
        return;
    }

    // =====================================================
    // COMMANDE : id
    // =====================================================
    if (input.equalsIgnoreCase("id")) {
        Serial.print("[MAIN] ID machine actuel : '");
        Serial.print(machineIdentity.getId());
        Serial.println("'");
        return;
    }

    // =====================================================
    // COMMANDE : setid <password> <newId>
    // =====================================================
    if (input.startsWith("setid ")) {
        int firstSpace = input.indexOf(' ');
        int secondSpace = input.indexOf(' ', firstSpace + 1);

        if (secondSpace < 0) {
            Serial.println("[MAIN] ❌ Usage: setid <password> <newId>");
            Serial.println("[MAIN] Exemple: setid MonMotDePasse 00042");
            return;
        }

        String password = input.substring(firstSpace + 1, secondSpace);
        String newId = input.substring(secondSpace + 1);
        newId.trim();

        if (password != AppSecrets::AdminConfig::SERIAL_PASSWORD) {
            Serial.println("[MAIN] ❌ Mot de passe invalide.");
            remoteLog.error("SECURITY", "Tentative setid echouee (mauvais mot de passe)");
            return;
        }

        if (machineIdentity.setId(newId)) {
            Serial.println("[MAIN] ✅ ID changé. Redémarrage dans 2 secondes...");
            remoteLog.info("IDENTITY", "ID change, redemarrage");
            delay(2000);
            ESP.restart();
        } else {
            Serial.println("[MAIN] ❌ Échec du changement d'ID.");
            remoteLog.error("IDENTITY", "Echec changement ID");
        }
        return;
    }

    // =====================================================
    // COMMANDE : info
    // =====================================================
    if (input.equalsIgnoreCase("info")) {
        Serial.println("==============================================");
        Serial.println("  INFORMATIONS SYSTÈME");
        Serial.println("==============================================");
        Serial.print("Machine ID       : ");
        Serial.println(machineIdentity.getId());
        Serial.print("Firmware version : ");
        Serial.println(AppConfig::Machine::FIRMWARE_VERSION);
        Serial.print("Uptime           : ");
        Serial.print(millis() / 1000);
        Serial.println(" s");
        Serial.print("Free heap        : ");
        Serial.println(ESP.getFreeHeap());
        Serial.print("MAC STA          : ");
        Serial.println(WiFi.macAddress());
        Serial.print("MAC SoftAP       : ");
        Serial.println(WiFi.softAPmacAddress());
        Serial.print("WiFi status      : ");
        Serial.println(WiFi.status());
        Serial.print("MQTT connected   : ");
        Serial.println(mqttManager.isConnected() ? "YES" : "NO");
        Serial.print("System state     : ");
        Serial.println(systemStateToString(currentState));

        printSecurityInfo();
        return;
    }

    // =====================================================
    // COMMANDES DE TEST DISTRIBUTION
    // =====================================================
    const AppConfig::Tariff* selectedTariff = nullptr;
    const char* testPaymentMethod = nullptr;

    if (input == "1") {
        selectedTariff = &AppConfig::TARIFFS[0];  // 50 FCFA
        testPaymentMethod = "coin";
    }
    else if (input == "2") {
        selectedTariff = &AppConfig::TARIFFS[1];  // 100 FCFA
        testPaymentMethod = "coin";
    }
    else if (input == "3") {
        selectedTariff = &AppConfig::TARIFFS[2];  // 200 FCFA
        testPaymentMethod = "coin";
    }
    else if (input == "w1") {
        selectedTariff = &AppConfig::TARIFFS[0];
        testPaymentMethod = "wave";
    }
    else if (input == "w2") {
        selectedTariff = &AppConfig::TARIFFS[1];
        testPaymentMethod = "wave";
    }
    else if (input == "o1") {
        selectedTariff = &AppConfig::TARIFFS[0];
        testPaymentMethod = "orange_money";
    }
    else if (input == "o2") {
        selectedTariff = &AppConfig::TARIFFS[1];
        testPaymentMethod = "orange_money";
    }
    else {
        printSerialHelp();
        return;
    }

    Serial.println("----------------------------------------------");
    Serial.print("[TEST] Type paiement : ");
    Serial.println(testPaymentMethod);
    Serial.print("[TEST] Montant : ");
    Serial.print(selectedTariff->amountFcfa);
    Serial.println(" FCFA");
    Serial.print("[TEST] Impulsions : ");
    Serial.println(selectedTariff->pulses);
    Serial.println("----------------------------------------------");

    char transactionId[40];
    snprintf(transactionId, sizeof(transactionId), "SERIAL-%s-%lu",
             testPaymentMethod, millis());

    CommandValidator::DispenseCommand command = {
        "DISPENSE",
        machineIdentity.getId().c_str(),
        transactionId,
        selectedTariff->amountFcfa,
        testPaymentMethod
    };

    CommandValidator::ValidationResult validation;

    if (!commandValidator.validateDispenseCommand(command, validation)) {
        Serial.print("[MAIN][SECURITY] Commande refusée : ");
        Serial.println(validation.message);
        remoteLog.warn("SECURITY", validation.message);
        telemetryService.publishError(validation.errorCode, validation.message);
        setSystemState(SystemState::IDLE);
        return;
    }

    Serial.println("[MAIN][SECURITY] Commande validée.");

    disableCoinInputDuringCreditEmission();

    if (transactionManager.startTransaction(
            selectedTariff->amountFcfa,
            transactionId,
            testPaymentMethod
        )) {
        setSystemState(SystemState::DISPENSING);
    } else {
        enableCoinInputAfterCreditEmission();
    }
}


// =====================================================
// MQTT
// =====================================================

bool mqttPublishAdapter(const char* topic, const char* payload) {
    return mqttManager.publish(topic, payload);
}

bool syncPendingTransactionsToMqtt(const char* topic, const char* payload) {
    return mqttManager.publish(topic, payload);
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
        remoteLog.error("MQTT", "Payload JSON invalide");
        telemetryService.publishError("INVALID_JSON", "Payload MQTT JSON invalide.");
        return;
    }

    const char* action = doc["action"] | "";
    const char* machineId = doc["machineId"] | "";
    const char* transactionId = doc["transactionId"] | "";
    const char* source = doc["source"] | "mqtt";
    uint16_t amountFcfa = doc["amountFcfa"] | 0;

    // =====================================================
    // COMMANDES ADMIN
    // =====================================================
    if (strcmp(action, "OTA_UPDATE") == 0) {
        Serial.println("[MAIN][MQTT][ADMIN] Commande OTA reçue.");
        remoteLog.info("OTA", "Commande OTA_UPDATE recue");
        otaService.checkAndPerformUpdate();
        return;
    }

    if (strcmp(action, "REBOOT") == 0) {
        Serial.println("[MAIN][MQTT][ADMIN] Commande REBOOT reçue.");
        remoteLog.warn("ADMIN", "Reboot demande a distance");
        delay(500);
        ESP.restart();
        return;
    }

    if (strcmp(action, "SET_ID") == 0) {
        const char* newId = doc["newId"] | "";
        const char* mqttPassword = doc["password"] | "";

        Serial.print("[MAIN][MQTT][ADMIN] Commande SET_ID : '");
        Serial.print(newId);
        Serial.println("'");

        // ✅ Vérification du mot de passe admin
        if (strcmp(mqttPassword, AppSecrets::AdminConfig::SERIAL_PASSWORD) != 0) {
            Serial.println("[MAIN][MQTT][SECURITY] ❌ Mot de passe admin invalide.");
            remoteLog.error("SECURITY", "Tentative SET_ID echouee (mauvais mot de passe)");
            telemetryService.publishError("INVALID_ADMIN_PASSWORD", "Mot de passe admin invalide.");
            return;
        }

        remoteLog.info("IDENTITY", "Changement ID demande");

        if (machineIdentity.setId(String(newId))) {
            telemetryService.publishError("ID_CHANGED", "Redémarrage imminent.");
            delay(1000);
            ESP.restart();
        } else {
            remoteLog.error("IDENTITY", "ID invalide");
            telemetryService.publishError("ID_CHANGE_FAILED", "ID invalide.");
        }
        return;
    }

    // =====================================================
    // DISPENSE
    // =====================================================
    CommandValidator::DispenseCommand command = {
        action,
        machineId,
        transactionId,
        amountFcfa,
        source
    };

    CommandValidator::ValidationResult validation;

    if (!commandValidator.validateDispenseCommand(command, validation)) {
        Serial.println("[MAIN][MQTT][SECURITY] Commande MQTT refusée.");
        remoteLog.warn("SECURITY", validation.message);
        telemetryService.publishError(validation.errorCode, validation.message);
        return;
    }

    if (isDuplicateMqttTransactionId(transactionId)) {
        remoteLog.warn("MQTT", "Transaction MQTT dupliquee");
        telemetryService.publishError(
            "DUPLICATE_MQTT_TRANSACTION",
            "Commande MQTT ignorée : transactionId déjà traité."
        );
        return;
    }

    const bool machineCanAcceptPayment = isMachineCanAcceptPayment();
    telemetryService.publishMachineAvailabilityStatus(machineCanAcceptPayment);

    if (!machineCanAcceptPayment) {
        rememberMqttTransactionId(transactionId);
        remoteLog.warn("MQTT", "Machine indisponible (COUNTER LOW)");
        telemetryService.publishError(
            "MACHINE_UNAVAILABLE_COUNTER_LOW",
            "Commande DISPENSE refusée : machine indisponible."
        );
        return;
    }

    rememberMqttTransactionId(transactionId);
    disableCoinInputDuringCreditEmission();

    if (!transactionManager.startTransaction(amountFcfa, transactionId, source)) {
        remoteLog.error("TRANSACTION", "Impossible de demarrer");
        telemetryService.publishError(
            "TRANSACTION_START_FAILED",
            "Impossible de démarrer la transaction MQTT."
        );
        enableCoinInputAfterCreditEmission();
        return;
    }

    setSystemState(SystemState::DISPENSING);
}


// =====================================================
// DEBUG
// =====================================================

void debugRawCoinInputPeriodic() {
    if (!AppConfig::Debug::COIN_INPUT_RAW_LOG_ENABLED) return;

    const uint32_t now = millis();
    if (now - lastRawDebugMs < AppConfig::Debug::COIN_INPUT_RAW_LOG_INTERVAL_MS) return;

    lastRawDebugMs = now;
    const int level = digitalRead(AppConfig::Pins::COIN_INPUT_PIN);
    Serial.print("[DEBUG][GPIO");
    Serial.print(AppConfig::Pins::COIN_INPUT_PIN);
    Serial.print("] Niveau brut = ");
    Serial.println(level == HIGH ? "HIGH" : "LOW");
}


// =====================================================
// MACHINE AVAILABILITY
// =====================================================

bool isMachineCanAcceptPayment() {
    return digitalRead(AppConfig::Pins::MACHINE_AVAILABLE_PIN) == HIGH;
}

void updateMachineAvailabilityStatus(bool forcePublish) {
    const bool machineCanAcceptPayment = isMachineCanAcceptPayment();
    const uint32_t now = millis();

    const bool changed = !hasPublishedMachineAvailability
                      || machineCanAcceptPayment != lastPublishedMachineAvailability;
    const bool periodic = now - lastMachineAvailabilityPublishMs
                       >= MACHINE_AVAILABILITY_PERIODIC_PUBLISH_MS;

    if (!forcePublish && !changed && !periodic) return;

    telemetryService.publishMachineAvailabilityStatus(machineCanAcceptPayment);

    lastPublishedMachineAvailability = machineCanAcceptPayment;
    hasPublishedMachineAvailability = true;
    lastMachineAvailabilityPublishMs = now;

    Serial.print("[MACHINE][COUNTER] Status publié : ");
    Serial.print(machineCanAcceptPayment ? "AVAILABLE" : "UNAVAILABLE");

    const int rawPin = digitalRead(AppConfig::Pins::MACHINE_AVAILABLE_PIN);
    Serial.print(" (GPIO");
    Serial.print(AppConfig::Pins::MACHINE_AVAILABLE_PIN);
    Serial.print(" = ");
    Serial.print(rawPin == HIGH ? "HIGH" : "LOW");
    Serial.println(")");
}


// =====================================================
// SETUP
// =====================================================

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("=== BOOT DEMARRAGE ===");

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

    remoteLog.begin(mqttPublishAdapter);

    telemetryService.publishBoot();

    setSystemState(SystemState::IDLE);

    Serial.println("[BOOT] Initialisation terminée.");

    remoteLog.info("BOOT", "Firmware demarre");
}


// =====================================================
// LOOP
// =====================================================

void loop() {
    wifiManager.update();
    mqttManager.update();

    if (mqttManager.isConnected() && transactionStore.hasPendingSync()) {
        transactionStore.syncPendingTransactions(
            syncPendingTransactionsToMqtt,
            machineIdentity.topicEvents().c_str()
        );
    }

    updateMachineAvailabilityStatus(false);
    debugRawCoinInputPeriodic();
    updateHeartbeat();
    handleSerialPulseTest();

    otaService.update();

    // =====================================================
    // MONNAYEUR PHYSIQUE
    // =====================================================
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

                char coinMsg[120];
                snprintf(coinMsg, sizeof(coinMsg),
                         "PIECE RECUE - %u FCFA (%u impulsions) - Machine %s",
                         amount,
                         pulses,
                         machineIdentity.getId().c_str());
                remoteLog.info("COIN_DETECTED", coinMsg);

                if (isDuplicateCoinPaymentEvent(amount, pulses)) {
                    Serial.println("[MAIN][ANTI-DOUBLON] Paiement physique déjà publié récemment.");
                } else {
                    String eventId = buildCoinPaymentEventId(amount, pulses);

                    const bool published = telemetryService.publishCoinPaymentEvent(
                        amount,
                        pulses,
                        "physical_coin",
                        eventId.c_str()
                    );

                    if (published) {
                        Serial.print("[MAIN] Paiement physique publié. Event ID : ");
                        Serial.println(eventId);
                        rememberCoinPaymentEvent(amount, pulses);
                    } else {
                        Serial.print("[MAIN][WARN] Publication MQTT échouée. Event ID : ");
                        Serial.println(eventId);
                        remoteLog.warn("COIN", "Publication MQTT echouee, mise en file");

                        transactionStore.enqueueOfflineTransaction(
                            eventId.c_str(),
                            amount,
                            "physical_coin",
                            TransactionStore::Status::CREATED,
                            pulses
                        );
                    }
                }

                setSystemState(SystemState::IDLE);
            }

            Serial.println("----------------------------------------------");
        }
    }

    // =====================================================
    // TRANSACTION MANAGER
    // =====================================================
    transactionManager.update();

    // ===== TRANSACTION RÉUSSIE =====
    if (transactionManager.hasSucceeded()) {
        const char* source = transactionManager.getLastCompletedSource();
        const char* paymentMethod = detectPaymentMethod(source);
        const char* label = paymentMethodToLabel(paymentMethod);

        char txMsg[180];
        snprintf(txMsg, sizeof(txMsg),
                 "PAIEMENT %s REUSSI - %u FCFA - Machine %s - Tx %s",
                 label,
                 transactionManager.getLastCompletedAmountFcfa(),
                 machineIdentity.getId().c_str(),
                 transactionManager.getLastCompletedTransactionId());
        remoteLog.info("PAYMENT_OK", txMsg);

        telemetryService.publishTransactionEvent(
            "transaction_success",
            transactionManager.getLastCompletedTransactionId(),
            transactionManager.getLastCompletedAmountFcfa(),
            source,
            "SUCCESS",
            paymentMethod
        );

        telemetryService.publishPaymentEvent(
            paymentMethod,
            transactionManager.getLastCompletedAmountFcfa(),
            transactionManager.getLastCompletedTransactionId(),
            "SUCCESS",
            source
        );

        enableCoinInputAfterCreditEmission();
        setSystemState(AppConfig::SystemState::IDLE);
        Serial.println("[MAIN] Transaction confirmée par TransactionManager.");
    }

    // ===== TRANSACTION ÉCHOUÉE =====
    if (transactionManager.hasFailed()) {
        const char* source = transactionManager.getCurrentSource();
        const char* paymentMethod = detectPaymentMethod(source);
        const char* label = paymentMethodToLabel(paymentMethod);

        char txMsg[180];
        snprintf(txMsg, sizeof(txMsg),
                 "PAIEMENT %s ECHOUE - %u FCFA - Machine %s - Tx %s",
                 label,
                 transactionManager.getCurrentAmountFcfa(),
                 machineIdentity.getId().c_str(),
                 transactionManager.getCurrentTransactionId());
        remoteLog.error("PAYMENT_FAIL", txMsg);

        telemetryService.publishTransactionEvent(
            "transaction_failed",
            transactionManager.getCurrentTransactionId(),
            transactionManager.getCurrentAmountFcfa(),
            source,
            "FAILED",
            paymentMethod
        );

        enableCoinInputAfterCreditEmission();
        setSystemState(SystemState::IDLE);
        Serial.println("[MAIN][WARN] Transaction échouée.");
    }
}