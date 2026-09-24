// src/hardware/PulseOutput.cpp

#include "PulseOutput.h"

PulseOutput::PulseOutput(uint8_t outputPin)
    : _outputPin(outputPin),
      _state(State::IDLE),
      _requestedPulses(0),
      _sentPulses(0),
      _lastChangeMs(0),
      _totalPulsesSent(0),
      _completionFlag(false) {
}

void PulseOutput::begin() {
    pinMode(_outputPin, OUTPUT);
    setOutputHigh();

    _state = State::IDLE;
    _requestedPulses = 0;
    _sentPulses = 0;
    _lastChangeMs = 0;
    _completionFlag = false;

    Serial.println("[PulseOutput] Module initialisé.");
}

bool PulseOutput::requestPulses(uint8_t pulseCount) {
    if (_state != State::IDLE) {
        Serial.println("[PulseOutput][REFUS] Module déjà occupé.");
        return false;
    }

    if (pulseCount == 0) {
        Serial.println("[PulseOutput][REFUS] Nombre d'impulsions nul.");
        return false;
    }

    if (pulseCount > AppConfig::Limits::MAX_OUTPUT_PULSES) {
        Serial.println("[PulseOutput][REFUS] Nombre d'impulsions trop élevé.");
        _state = State::ERROR;
        return false;
    }

    _requestedPulses = pulseCount;
    _sentPulses = 0;
    _completionFlag = false;

    setOutputLow();
    _lastChangeMs = millis();
    _state = State::PULSE_LOW;

    Serial.print("[PulseOutput] Démarrage émission : ");
    Serial.print(_requestedPulses);
    Serial.println(" impulsions.");

    return true;
}

void PulseOutput::update() {
    const uint32_t now = millis();

    switch (_state) {
        case State::IDLE:
            break;

        case State::PULSE_LOW:
            if (now - _lastChangeMs >= AppConfig::Timing::PULSE_HIGH_MS) {
                setOutputHigh();

                _sentPulses++;
                _totalPulsesSent++;

                _lastChangeMs = now;
                _state = State::PULSE_HIGH;

                Serial.print("[PulseOutput] Impulsion envoyée : ");
                Serial.print(_sentPulses);
                Serial.print("/");
                Serial.println(_requestedPulses);
            }
            break;

        case State::PULSE_HIGH:
            if (now - _lastChangeMs >= AppConfig::Timing::PULSE_LOW_MS) {
                if (_sentPulses >= _requestedPulses) {
                    _state = State::COMPLETED;
                    _completionFlag = true;

                    Serial.println("[PulseOutput] Émission terminée.");
                } else {
                    setOutputLow();
                    _lastChangeMs = now;
                    _state = State::PULSE_LOW;
                }
            }
            break;

        case State::COMPLETED:
            _state = State::IDLE;
            break;

        case State::ERROR:
            setOutputHigh();
            break;
    }
}

bool PulseOutput::isBusy() const {
    return _state == State::PULSE_HIGH || _state == State::PULSE_LOW;
}

bool PulseOutput::hasCompleted() {
    if (_completionFlag) {
        _completionFlag = false;
        return true;
    }

    return false;
}

void PulseOutput::cancel() {
    setOutputHigh();

    _state = State::IDLE;
    _requestedPulses = 0;
    _sentPulses = 0;
    _completionFlag = false;

    Serial.println("[PulseOutput] Émission annulée.");
}

PulseOutput::State PulseOutput::getState() const {
    return _state;
}

uint8_t PulseOutput::getRequestedPulses() const {
    return _requestedPulses;
}

uint8_t PulseOutput::getSentPulses() const {
    return _sentPulses;
}

uint32_t PulseOutput::getTotalPulsesSent() const {
    return _totalPulsesSent;
}

void PulseOutput::setOutputHigh() {
    digitalWrite(_outputPin, LOW);
}

void PulseOutput::setOutputLow() {
    digitalWrite(_outputPin, HIGH);
}