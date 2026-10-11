// src/security/SerialGuard.cpp

#include "SerialGuard.h"

SerialGuard serialGuard;

SerialGuard::SerialGuard()
    : _authenticated(false),
      _enabled(true),
      _authAttempts(0),
      _lastPromptMs(0) {
    _inputBuffer.reserve(64);
}

void SerialGuard::begin() {
    _preferences.begin("security", false);

    if (!_preferences.isKey("pwd_hash")) {
        Serial.println("[SerialGuard] Premier demarrage : mot de passe par defaut = admin1234");
        _preferences.putString("pwd_hash", hashPassword(DEFAULT_PASSWORD));
    }

    if (_enabled) {
        promptForPassword();
    } else {
        _authenticated = true;
    }
}

void SerialGuard::update() {
    if (!_enabled || _authenticated) return;
    handlePasswordInput();
}

void SerialGuard::lock() {
    _authenticated = false;
    _inputBuffer   = "";
    _authAttempts  = 0;
    if (_enabled) promptForPassword();
}

void SerialGuard::promptForPassword() {
    _authenticated = false;
    _authAttempts  = 0;
    _inputBuffer   = "";
    _lastPromptMs  = millis();

    Serial.println();
    Serial.println("==============================================");
    Serial.println("   ACCES PROTEGE - MOT DE PASSE REQUIS");
    Serial.println("==============================================");
    Serial.println("Entrez le mot de passe :");
    Serial.print("> ");
}

void SerialGuard::handlePasswordInput() {
    while (Serial.available()) {
        char c = Serial.read();

        if (c == '\n' || c == '\r') {
            if (_inputBuffer.length() == 0) continue;

            String inputHash  = hashPassword(_inputBuffer);
            String storedHash = _preferences.getString("pwd_hash", "");

            if (inputHash == storedHash) {
                _authenticated = true;
                Serial.println();
                Serial.println("OK - Authentification reussie !");
                Serial.println("==============================================");
                Serial.println();
            } else {
                _authAttempts++;
                Serial.println();
                Serial.print("Mot de passe incorrect. Tentative ");
                Serial.print(_authAttempts);
                Serial.print("/");
                Serial.println(MAX_AUTH_ATTEMPTS);

                if (_authAttempts >= MAX_AUTH_ATTEMPTS) {
                    printAccessDenied();
                    delay(10000);
                    promptForPassword();
                } else {
                    Serial.print("> ");
                }
            }
            _inputBuffer = "";
        } else if (c >= 32 && c <= 126) {
            _inputBuffer += c;
            Serial.print("*");
        }
    }

    if (millis() - _lastPromptMs > AUTH_TIMEOUT_MS && _inputBuffer.length() == 0) {
        Serial.println();
        Serial.println("[SerialGuard] Timeout. Nouvelle demande...");
        promptForPassword();
    }
}

void SerialGuard::printAccessDenied() {
    Serial.println();
    Serial.println("==============================================");
    Serial.println("   ACCES REFUSE - TROP DE TENTATIVES");
    Serial.println("==============================================");
    Serial.println("Patientez 10 secondes...");
    Serial.println();
}

bool SerialGuard::changePassword(const String& oldPassword, const String& newPassword) {
    String storedHash = _preferences.getString("pwd_hash", "");

    if (hashPassword(oldPassword) != storedHash) {
        Serial.println("[SerialGuard] Ancien mot de passe incorrect.");
        return false;
    }

    if (newPassword.length() < 6) {
        Serial.println("[SerialGuard] Nouveau mot de passe trop court (min 6).");
        return false;
    }

    _preferences.putString("pwd_hash", hashPassword(newPassword));
    Serial.println("[SerialGuard] Mot de passe change avec succes.");
    return true;
}

String SerialGuard::hashPassword(const String& password) const {
    // Hash simple djb2 + salt base sur la MAC eFuse (unique par ESP32)
    uint32_t hash = 5381;
    for (size_t i = 0; i < password.length(); i++) {
        hash = ((hash << 5) + hash) + password[i];
    }

    uint64_t chipMac = ESP.getEfuseMac();
    for (int i = 0; i < 8; i++) {
        hash = ((hash << 5) + hash) + ((chipMac >> (i * 8)) & 0xFF);
    }

    char result[16];
    snprintf(result, sizeof(result), "%08X", hash);
    return String(result);
}