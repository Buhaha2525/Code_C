// src/hardware/CoinAcceptor.cpp

#include "CoinAcceptor.h"

/*
   Mutex utilisé pour protéger les variables partagées
   entre l'interruption et la boucle principale.
*/
static portMUX_TYPE coinMux = portMUX_INITIALIZER_UNLOCKED;

static uint16_t lastDebugPulseCount = 0;

CoinAcceptor* CoinAcceptor::_instance = nullptr;

CoinAcceptor::CoinAcceptor(uint8_t inputPin)
    : _inputPin(inputPin),
      _pulseCount(0),
      _lastPulseUs(0),
      _pulseDetected(false),
      _enabled(true),
      _eventReady(false),
      _lastCompletedPulseCount(0),
      _lastAmountFcfa(0) {
}

void CoinAcceptor::begin() {
    _instance = this;

    pinMode(_inputPin, INPUT_PULLUP);

    _pulseCount = 0;
    _lastPulseUs = 0;
    _pulseDetected = false;
    _eventReady = false;
    _lastCompletedPulseCount = 0;
    _lastAmountFcfa = 0;

    /*
       La plupart des monnayeurs donnent une impulsion vers GND.
       Donc on écoute FALLING.

       Si ton monnayeur fonctionne à l'inverse, on changera FALLING par RISING.
    */
    attachInterrupt(
        digitalPinToInterrupt(_inputPin),
        CoinAcceptor::handleInterruptStatic,
        FALLING
    );

    Serial.println("[CoinAcceptor] Module initialisé.");
}

void CoinAcceptor::update() {
    if (!_enabled) {
    return;
}
    uint16_t currentPulseCount = 0;
    uint32_t lastPulseUsCopy = 0;
    bool pulseDetectedCopy = false;

    portENTER_CRITICAL(&coinMux);
    lastDebugPulseCount = 0;
    currentPulseCount = _pulseCount;
    lastPulseUsCopy = _lastPulseUs;
    pulseDetectedCopy = _pulseDetected;
    portEXIT_CRITICAL(&coinMux);
   static uint16_t lastDebugPulseCount = 0;

if (currentPulseCount > 0 && currentPulseCount != lastDebugPulseCount) {
    lastDebugPulseCount = currentPulseCount;

    Serial.print("[CoinAcceptor][DEBUG] Impulsion détectée. Total actuel : ");
    Serial.println(currentPulseCount);
}
    if (!pulseDetectedCopy || currentPulseCount == 0) {
        return;
    }

    const uint32_t nowUs = micros();
    const uint32_t timeoutUs = AppConfig::Timing::COIN_END_TIMEOUT_MS * 1000UL;

    /*
       Si aucune nouvelle impulsion n'arrive pendant le délai configuré,
       on considère que la pièce ou la séquence est terminée.
    */
    if (nowUs - lastPulseUsCopy >= timeoutUs) {
        _lastCompletedPulseCount = currentPulseCount;
        _lastAmountFcfa = convertPulsesToAmount(currentPulseCount);
        _eventReady = true;

        portENTER_CRITICAL(&coinMux);
        _pulseCount = 0;
        _pulseDetected = false;
        _lastPulseUs = 0;
        portEXIT_CRITICAL(&coinMux);

        Serial.print("[CoinAcceptor] Séquence terminée : ");
        Serial.print(_lastCompletedPulseCount);
        Serial.println(" impulsions.");

        if (_lastAmountFcfa > 0) {
            Serial.print("[CoinAcceptor] Montant reconnu : ");
            Serial.print(_lastAmountFcfa);
            Serial.println(" FCFA.");
        } else {
            Serial.println("[CoinAcceptor][WARN] Nombre d'impulsions non reconnu.");
        }
    }
}

bool CoinAcceptor::hasCoinEvent() {
    if (!_enabled) {
        return false;
    }

    if (!_eventReady) {
        return false;
    }

    _eventReady = false;
    return true;
}

uint16_t CoinAcceptor::getLastAmountFcfa() const {
    return _lastAmountFcfa;
}

uint16_t CoinAcceptor::getLastPulseCount() const {
    return _lastCompletedPulseCount;
}

void CoinAcceptor::reset() {
    portENTER_CRITICAL(&coinMux);
    _pulseCount = 0;
    _lastPulseUs = 0;
    _pulseDetected = false;
    portEXIT_CRITICAL(&coinMux);

    _eventReady = false;
    _lastCompletedPulseCount = 0;
    _lastAmountFcfa = 0;

    Serial.println("[CoinAcceptor] Reset effectué.");
}

void CoinAcceptor::enable() {
    detachInterrupt(digitalPinToInterrupt(_inputPin));

    portENTER_CRITICAL(&coinMux);
    _pulseCount = 0;
    _lastPulseUs = micros();
    _pulseDetected = false;
    portEXIT_CRITICAL(&coinMux);

    _eventReady = false;
    _lastCompletedPulseCount = 0;
    _lastAmountFcfa = 0;

    _enabled = true;

    attachInterrupt(
        digitalPinToInterrupt(_inputPin),
        CoinAcceptor::handleInterruptStatic,
        FALLING
    );

    Serial.println("[CoinAcceptor] Activé.");
}

void CoinAcceptor::disable() {
    _enabled = false;

    detachInterrupt(digitalPinToInterrupt(_inputPin));

    portENTER_CRITICAL(&coinMux);
    _pulseCount = 0;
    _lastPulseUs = micros();
    _pulseDetected = false;
    portEXIT_CRITICAL(&coinMux);

    _eventReady = false;
    _lastCompletedPulseCount = 0;
    _lastAmountFcfa = 0;

    Serial.println("[CoinAcceptor] Désactivé.");
}

void IRAM_ATTR CoinAcceptor::handleInterruptStatic() {
    if (_instance != nullptr) {
        _instance->handleInterrupt();
    }
}

void IRAM_ATTR CoinAcceptor::handleInterrupt() {
    if (!_enabled) {
        return;
    }

    const uint32_t nowUs = micros();

    portENTER_CRITICAL_ISR(&coinMux);

    if (!_enabled) {
        portEXIT_CRITICAL_ISR(&coinMux);
        return;
    }

    if (nowUs - _lastPulseUs >= AppConfig::Timing::COIN_DEBOUNCE_US) {
        _pulseCount++;
        _lastPulseUs = nowUs;
        _pulseDetected = true;
    }

    portEXIT_CRITICAL_ISR(&coinMux);
}

uint16_t CoinAcceptor::convertPulsesToAmount(uint16_t pulseCount) const {
    return AppConfig::Money::pulseCountToAmount(pulseCount);
}