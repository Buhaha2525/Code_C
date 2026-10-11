// src/security/SerialGuard.h

#pragma once

#include <Arduino.h>
#include <Preferences.h>

class SerialGuard {
public:
    SerialGuard();

    void begin();       // À appeler dans setup()
    void update();      // À appeler dans loop()
    void lock();        // Reverrouiller manuellement

    bool isAuthenticated() const { return _authenticated; }
    bool isEnabled() const { return _enabled; }
    void setEnabled(bool enabled) { _enabled = enabled; }

    bool changePassword(const String& oldPassword, const String& newPassword);

private:
    Preferences _preferences;

    bool     _authenticated;
    bool     _enabled;
    uint8_t  _authAttempts;
    String   _inputBuffer;
    uint32_t _lastPromptMs;

    static constexpr uint8_t  MAX_AUTH_ATTEMPTS = 3;
    static constexpr uint32_t AUTH_TIMEOUT_MS   = 30000;
    static constexpr const char* DEFAULT_PASSWORD = "admin1234";

    String hashPassword(const String& password) const;
    void   promptForPassword();
    void   handlePasswordInput();
    void   printAccessDenied();
};

extern SerialGuard serialGuard;