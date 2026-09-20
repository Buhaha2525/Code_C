// src/security/CommandValidator.h

#pragma once

#include <Arduino.h>
#include "../config/config.h"
#include "../config/MachineIdentity.h"

class CommandValidator {
public:
    struct DispenseCommand {
        const char* action;
        const char* machineId;
        const char* transactionId;
        uint16_t amountFcfa;
        const char* source;
    };

    struct ValidationResult {
        bool valid;
        const char* errorCode;
        const char* message;
        uint8_t pulseCount;
    };

    CommandValidator();

    void begin();

    bool validateDispenseCommand(
        const DispenseCommand& command,
        ValidationResult& result
    ) const;

    bool isAmountAllowed(uint16_t amountFcfa) const;

    bool findPulseCountForAmount(
        uint16_t amountFcfa,
        uint8_t& pulseCount
    ) const;

private:
    bool isTextValid(const char* text) const;
    bool isActionValid(const char* action) const;
    bool isMachineIdValid(const char* machineId) const;
    bool isTransactionIdValid(const char* transactionId) const;

    void setResult(
        ValidationResult& result,
        bool valid,
        const char* errorCode,
        const char* message,
        uint8_t pulseCount = 0
    ) const;
};