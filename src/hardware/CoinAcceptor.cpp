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
       On écoute CHANGE pour mesurer précisément la durée de chaque impulsion
       et ainsi filtrer les bruits électriques très courts (parasites).
    */
    attachInterrupt(
        digitalPinToInterrupt(_inputPin),
        CoinAcceptor::handleInterruptStatic,
        CHANGE
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
    currentPulseCount = _pulseCount;
    lastPulseUsCopy = _lastPulseUs;
    pulseDetectedCopy = _pulseDetected;
    portEXIT_CRITICAL(&coinMux);

    if (currentPulseCount > 0 && currentPulseCount != lastDebugPulseCount) {
        lastDebugPulseCount = currentPulseCount;

        Serial.print("[CoinAcceptor][DEBUG] Impulsion valide détectée. Total actuel : ");
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

        lastDebugPulseCount = 0;

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
    lastDebugPulseCount = 0;

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
    lastDebugPulseCount = 0;

    _enabled = true;

    attachInterrupt(
        digitalPinToInterrupt(_inputPin),
        CoinAcceptor::handleInterruptStatic,
        CHANGE
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
    lastDebugPulseCount = 0;

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
    const int pinLevel = digitalRead(_inputPin);
    static uint32_t fallTimeUs = 0;

    portENTER_CRITICAL_ISR(&coinMux);

    if (!_enabled) {
        portEXIT_CRITICAL_ISR(&coinMux);
        return;
    }

    // Le monnayeur passe à 0V (front descendant)
    if (pinLevel == LOW) {
        fallTimeUs = nowUs;
    } else {
        // Le monnayeur revient à 3.3V (front montant) : calcul de la durée
        const uint32_t pulseDurationUs = nowUs - fallTimeUs;

        // Une impulsion réelle de monnayeur dure entre 15ms et 150ms.
        // Les parasites d'électrovannes ou relais (< 1ms) sont donc ignorés.
        if (pulseDurationUs >= 15000 && pulseDurationUs <= 150000) {
            if (nowUs - _lastPulseUs >= AppConfig::Timing::COIN_DEBOUNCE_US) {
                _pulseCount++;
                _lastPulseUs = nowUs;
                _pulseDetected = true;
            }
        }
    }

    portEXIT_CRITICAL_ISR(&coinMux);
}

uint16_t CoinAcceptor::convertPulsesToAmount(uint16_t pulseCount) const {
    return AppConfig::Money::pulseCountToAmount(pulseCount);
}