// src/hardware/Dispenser.h

#pragma once

#include <Arduino.h>
#include "../config/config.h"
#include "PulseOutput.h"

/*
   =========================================================
   MODULE : Dispenser

   Rôle :
   - Recevoir une demande de distribution à partir d'un montant
   - Convertir le montant en nombre d'impulsions
   - Utiliser PulseOutput pour envoyer les impulsions
   - Suivre l'état de la distribution
   =========================================================
*/

class Dispenser {
public:
    enum class State {
        IDLE,
        DISPENSING,
        SUCCESS,
        FAILED
    };

    Dispenser();

    void begin(PulseOutput* pulseOutput);
    bool dispenseAmount(uint16_t amountFcfa, const char* source = "unknown");
    void update();

    bool isBusy() const;
    bool hasCompleted();
    bool hasFailed();

    State getState() const;
    uint16_t getCurrentAmountFcfa() const;
    uint8_t getCurrentPulseCount() const;
    const char* getCurrentSource() const;

private:
    PulseOutput* _pulseOutput;

    State _state;

    uint16_t _currentAmountFcfa;
    uint8_t _currentPulseCount;
    const char* _currentSource;

    bool _completionFlag;
    bool _failureFlag;

    bool findPulseCountForAmount(uint16_t amountFcfa, uint8_t& pulseCount) const;
};