// src/config/MachineIdentity.h

#pragma once

#include <Arduino.h>
#include <Preferences.h>

/*
   =========================================================
   GESTION DYNAMIQUE DE L'IDENTITÉ MACHINE
   =========================================================
   - Lit/écrit l'ID machine dans la NVS (Preferences)
   - Génère dynamiquement les topics MQTT
   - Persiste à travers reboots et OTA
   =========================================================
*/

class MachineIdentity {
public:
    void begin() {
        _preferences.begin("machine", false);

        if (_preferences.isKey("id")) {
            _machineId = _preferences.getString("id", "");
            Serial.print("[IDENTITY] ID chargé depuis NVS : '");
            Serial.print(_machineId);
            Serial.println("'");
        } else {
            Serial.println("[IDENTITY] Aucun ID en NVS. Utilisation de l'ID par défaut.");
            _machineId = DEFAULT_ID;
            _preferences.putString("id", _machineId);
        }

        if (_machineId.length() == 0) {
            _machineId = DEFAULT_ID;
            _preferences.putString("id", _machineId);
        }
    }

    const String& getId() const {
        return _machineId;
    }

    bool setId(const String& newId) {
        if (newId.length() == 0 || newId.length() > 64) {
            Serial.println("[IDENTITY] ❌ ID invalide (vide ou trop long).");
            return false;
        }

        for (size_t i = 0; i < newId.length(); i++) {
            char c = newId[i];
            bool ok = (c >= '0' && c <= '9')
                   || (c >= 'A' && c <= 'Z')
                   || (c >= 'a' && c <= 'z')
                   || c == '-' || c == '_';
            if (!ok) {
                Serial.print("[IDENTITY] ❌ Caractère invalide dans l'ID : ");
                Serial.println(c);
                return false;
            }
        }

        _machineId = newId;
        _preferences.putString("id", _machineId);

        Serial.print("[IDENTITY] ✅ Nouvel ID enregistré : '");
        Serial.print(_machineId);
        Serial.println("'");

        return true;
    }

    // =====================================================
    // Construction dynamique des topics MQTT
    // =====================================================
    String topicCommands() const  { return String("machines/") + _machineId + "/commands"; }
    String topicEvents() const    { return String("machines/") + _machineId + "/events"; }
    String topicTelemetry() const { return String("machines/") + _machineId + "/telemetry"; }
    String topicStatus() const    { return String("machines/") + _machineId + "/status"; }
    String topicAcks() const      { return String("machines/") + _machineId + "/acks"; }

private:
    static constexpr const char* DEFAULT_ID = "00001";
    Preferences _preferences;
    String      _machineId;
};

// Instance globale
extern MachineIdentity machineIdentity;