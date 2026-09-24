// src/hardware/Dispenser.cpp

#include "Dispenser.h"

Dispenser::Dispenser()
    : _pulseOutput(nullptr),
      _state(State::IDLE),
      _currentAmountFcfa(0),
      _currentPulseCount(0),
      _currentSource("unknown"),
      _completionFlag(false),
      _failureFlag(false) {
}

void Dispenser::begin(PulseOutput* pulseOutput) {
    _pulseOutput = pulseOutput;

    _state = State::IDLE;
    _currentAmountFcfa = 0;
    _currentPulseCount = 0;
    _currentSource = "unknown";
    _completionFlag = false;
    _failureFlag = false;

    Serial.println("[Dispenser] Module initialisé.");
}

bool Dispenser::dispenseAmount(uint16_t amountFcfa, const char* source) {
    if (_pulseOutput == nullptr) {
        Serial.println("[Dispenser][ERREUR] PulseOutput non initialisé.");
        _state = State::FAILED;
        _failureFlag = true;
        return false;
    }

    if (_state == State::DISPENSING || _pulseOutput->isBusy()) {
        Serial.println("[Dispenser][REFUS] Distribution déjà en cours.");
        return false;
    }

    uint8_t pulseCount = 0;

    if (!findPulseCountForAmount(amountFcfa, pulseCount)) {
        Serial.print("[Dispenser][REFUS] Montant non supporté : ");
        Serial.print(amountFcfa);
        Serial.println(" FCFA.");

        _state = State::FAILED;
        _failureFlag = true;
        return false;
    }

    _currentAmountFcfa = amountFcfa;
    _currentPulseCount = pulseCount;
    _currentSource = source;
    _completionFlag = false;
    _failureFlag = false;

    Serial.println("----------------------------------------------");
    Serial.print("[Dispenser] Demande de distribution reçue depuis : ");
    Serial.println(_currentSource);

    Serial.print("[Dispenser] Montant demandé : ");
    Serial.print(_currentAmountFcfa);
    Serial.println(" FCFA");

    Serial.print("[Dispenser] Impulsions à envoyer : ");
    Serial.println(_currentPulseCount);
    Serial.println("----------------------------------------------");

    if (!_pulseOutput->requestPulses(_currentPulseCount)) {
        Serial.println("[Dispenser][ERREUR] PulseOutput a refusé la demande.");
        _state = State::FAILED;
        _failureFlag = true;
        return false;
    }

    _state = State::DISPENSING;
    return true;
}

void Dispenser::update() {
    if (_pulseOutput == nullptr) {
        return;
    }

    _pulseOutput->update();

    if (_state == State::DISPENSING && _pulseOutput->hasCompleted()) {
        _state = State::SUCCESS;
        _completionFlag = true;

        Serial.println("----------------------------------------------");
        Serial.println("[Dispenser] Distribution terminée avec succès.");

        Serial.print("[Dispenser] Montant distribué : ");
        Serial.print(_currentAmountFcfa);
        Serial.println(" FCFA");

        Serial.print("[Dispenser] Impulsions envoyées : ");
        Serial.println(_currentPulseCount);
        Serial.println("----------------------------------------------");
    }
}

bool Dispenser::isBusy() const {
    return _state == State::DISPENSING;
}

bool Dispenser::hasCompleted() {
    if (_completionFlag) {
        _completionFlag = false;
        _state = State::IDLE;
        return true;
    }

    return false;
}

bool Dispenser::hasFailed() {
    if (_failureFlag) {
        _failureFlag = false;
        _state = State::IDLE;
        return true;
    }

    return false;
}

Dispenser::State Dispenser::getState() const {
    return _state;
}

uint16_t Dispenser::getCurrentAmountFcfa() const {
    return _currentAmountFcfa;
}

uint8_t Dispenser::getCurrentPulseCount() const {
    return _currentPulseCount;
}

const char* Dispenser::getCurrentSource() const {
    return _currentSource;
}

bool Dispenser::findPulseCountForAmount(uint16_t amountFcfa, uint8_t& pulseCount) const {
    pulseCount = AppConfig::Money::amountToPulseCount(amountFcfa);
    return pulseCount > 0;
}