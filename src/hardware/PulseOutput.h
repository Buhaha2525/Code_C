// src/hardware/PulseOutput.h

#pragma once

#include <Arduino.h>
#include "../config/config.h"

/*
   =========================================================
   MODULE : PulseOutput
   Rôle :
   - Envoyer des impulsions électriques sur une sortie GPIO
   - Remplacer les anciens delay() par une logique non bloquante
   - Préparer le système à fonctionner proprement avec MQTT,
     transactions et distribution réelle
   =========================================================
*/

class PulseOutput {
public:
    enum class State {
        IDLE,
        PULSE_HIGH,
        PULSE_LOW,
        COMPLETED,
        ERROR
    };

    explicit PulseOutput(uint8_t outputPin = AppConfig::Pins::PULSE_OUT_PIN);

    void begin();
    bool requestPulses(uint8_t pulseCount);
    void update();

    bool isBusy() const;
    bool hasCompleted();

    void cancel();

    State getState() const;
    uint8_t getRequestedPulses() const;
    uint8_t getSentPulses() const;
    uint32_t getTotalPulsesSent() const;

private:
    uint8_t _outputPin;
    State _state;

    uint8_t _requestedPulses;
    uint8_t _sentPulses;

    uint32_t _lastChangeMs;
    uint32_t _totalPulsesSent;

    bool _completionFlag;

    void setOutputHigh();
    void setOutputLow();
};