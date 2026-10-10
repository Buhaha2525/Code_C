// src/telemetry/LogService.h

#pragma once

#include <Arduino.h>
#include "config/MachineIdentity.h"

/*
   =========================================================
   LOG SERVICE — Envoi des logs vers MQTT
   =========================================================
   - Redirige Serial.print() vers machines/{ID}/logs
   - Bufferise par ligne (envoi à chaque '\n')
   - Chaque ligne = 1 message MQTT JSON
   - Compatible avec tous les outils MQTT (MQTTX, Node-RED, Grafana)
   =========================================================
*/

class LogService : public Print {
public:
    using PublishCallback = bool (*)(const char* topic, const char* payload);

    LogService()
        : _publishFn(nullptr), _bufferIndex(0), _enabled(false) {
        _buffer[0] = '\0';
    }

    // À appeler une fois dans setup(), APRÈS mqttManager.begin()
    void begin(PublishCallback publishFn) {
        _publishFn = publishFn;
        _enabled = true;
        _bufferIndex = 0;
        Serial.println("[LogService] Logs distants activés.");
    }

    void setEnabled(bool enabled) {
        _enabled = enabled;
    }

    // ✅ Appelé automatiquement par Serial.print-like
    size_t write(uint8_t c) override {
        // Toujours écrire sur le port USB
        Serial.write(c);

        if (!_enabled || _publishFn == nullptr) return 1;

        // Bufferiser jusqu'à '\n' ou buffer plein
        if (c == '\n' || _bufferIndex >= sizeof(_buffer) - 1) {
            _buffer[_bufferIndex] = '\0';
            if (_bufferIndex > 0) {
                sendLog(_buffer);
            }
            _bufferIndex = 0;
        } else {
            _buffer[_bufferIndex++] = (char)c;
        }

        return 1;
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        size_t n = 0;
        while (size--) n += write(*buffer++);
        return n;
    }

    // ✅ Logs structurés (recommandé pour filtrage côté outil)
    void logEvent(const char* level, const char* tag, const char* message) {
        Serial.print("[");
        Serial.print(level);
        Serial.print("][");
        Serial.print(tag);
        Serial.print("] ");
        Serial.println(message);

        if (!_enabled || _publishFn == nullptr) return;

        char payload[320];
        snprintf(payload, sizeof(payload),
                 "{\"level\":\"%s\",\"tag\":\"%s\",\"msg\":\"%s\",\"uptime\":%lu}",
                 level, tag, message, (unsigned long)millis());

        String topic = machineIdentity.topicLogs();
        _publishFn(topic.c_str(), payload);
    }

    void info(const char* tag, const char* msg)  { logEvent("INFO", tag, msg); }
    void warn(const char* tag, const char* msg)  { logEvent("WARN", tag, msg); }
    void error(const char* tag, const char* msg) { logEvent("ERROR", tag, msg); }
    void debug(const char* tag, const char* msg) { logEvent("DEBUG", tag, msg); }
    // Dans LogService.h, ajoute ces méthodes publiques :

    void logCoinDetected(uint16_t amount, uint16_t pulses) {
        char msg[100];
        snprintf(msg, sizeof(msg),
                 "PIECE RECUE - %u FCFA (%u impulsions)", amount, pulses);
        logEvent("INFO", "COIN", msg);
    }

    void logPaymentSuccess(const char* method, uint16_t amount, const char* txId) {
        char msg[120];
        snprintf(msg, sizeof(msg),
                 "PAIEMENT %s REUSSI - %u FCFA - Tx %s", method, amount, txId);
        logEvent("INFO", "PAYMENT_OK", msg);
    }

    void logPaymentFailed(const char* method, uint16_t amount, const char* txId, const char* reason) {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "PAIEMENT %s ECHOUE - %u FCFA - Tx %s - Raison: %s",
                 method, amount, txId, reason);
        logEvent("ERROR", "PAYMENT_FAIL", msg);
    }

    void logDispenseStart(uint16_t amount, uint8_t pulses, const char* source) {
        char msg[120];
        snprintf(msg, sizeof(msg),
                 "DISTRIBUTION DEMARREE - %u FCFA (%u impulsions) - Source: %s",
                 amount, pulses, source);
        logEvent("INFO", "DISPENSE", msg);
    }

    void logDispenseComplete(uint16_t amount, uint8_t pulses) {
        char msg[100];
        snprintf(msg, sizeof(msg),
                 "DISTRIBUTION TERMINEE - %u FCFA (%u impulsions)", amount, pulses);
        logEvent("INFO", "DISPENSE", msg);
    }
private:
    PublishCallback _publishFn;
    char            _buffer[256];
    size_t          _bufferIndex;
    bool            _enabled;

    void sendLog(const char* line) {
        if (strlen(line) == 0) return;

        // Échapper guillemets et backslash pour JSON
        char escaped[512];
        size_t j = 0;
        for (size_t i = 0; line[i] && j < sizeof(escaped) - 2; i++) {
            if (line[i] == '"' || line[i] == '\\') {
                escaped[j++] = '\\';
            }
            escaped[j++] = line[i];
        }
        escaped[j] = '\0';

        char payload[600];
        snprintf(payload, sizeof(payload),
                 "{\"level\":\"LOG\",\"msg\":\"%s\",\"uptime\":%lu}",
                 escaped, (unsigned long)millis());

        String topic = machineIdentity.topicLogs();
        _publishFn(topic.c_str(), payload);
    }
};


// Instance globale
extern LogService remoteLog;