// src/security/CommandValidator.cpp

#include "CommandValidator.h"

CommandValidator::CommandValidator() {
}

void CommandValidator::begin() {
    Serial.println("[CommandValidator] Module initialisé.");
}

bool CommandValidator::validateDispenseCommand(
    const DispenseCommand& command,
    ValidationResult& result
) const {
    setResult(result, false, "UNKNOWN", "Validation non effectuée.");

    if (!isActionValid(command.action)) {
        setResult(result, false, "INVALID_ACTION",
                  "Action invalide. Seule l'action DISPENSE est autorisée.");
        return false;
    }

    if (!isMachineIdValid(command.machineId)) {
        setResult(result, false, "INVALID_MACHINE_ID",
                  "La commande n'est pas destinée à cette machine.");
        return false;
    }

    if (!isTransactionIdValid(command.transactionId)) {
        setResult(result, false, "INVALID_TRANSACTION_ID",
                  "Transaction ID invalide ou vide.");
        return false;
    }

    if (command.amountFcfa == 0) {
        setResult(result, false, "INVALID_AMOUNT",
                  "Le montant ne peut pas être nul.");
        return false;
    }

    if (command.amountFcfa > AppConfig::Limits::MAX_ALLOWED_AMOUNT_FCFA) {
        setResult(result, false, "AMOUNT_TOO_HIGH",
                  "Le montant dépasse la limite maximale autorisée.");
        return false;
    }

    uint8_t pulseCount = 0;

    if (!findPulseCountForAmount(command.amountFcfa, pulseCount)) {
        setResult(result, false, "UNSUPPORTED_AMOUNT",
                  "Le montant ne correspond à aucun tarif autorisé.");
        return false;
    }

    setResult(result, true, "OK", "Commande DISPENSE valide.", pulseCount);

    return true;
}

bool CommandValidator::isAmountAllowed(uint16_t amountFcfa) const {
    uint8_t pulseCount = 0;
    return findPulseCountForAmount(amountFcfa, pulseCount);
}

bool CommandValidator::findPulseCountForAmount(
    uint16_t amountFcfa,
    uint8_t& pulseCount
) const {
    pulseCount = AppConfig::Money::amountToPulseCount(amountFcfa);
    return pulseCount > 0;
}

bool CommandValidator::isTextValid(const char* text) const {
    return text != nullptr && strlen(text) > 0;
}

bool CommandValidator::isActionValid(const char* action) const {
    if (!isTextValid(action)) {
        return false;
    }
    return strcmp(action, "DISPENSE") == 0;
}

bool CommandValidator::isMachineIdValid(const char* machineId) const {
    if (!isTextValid(machineId)) {
        return false;
    }

    // ✅ Comparaison avec l'ID dynamique
    return machineIdentity.getId() == String(machineId);
}

bool CommandValidator::isTransactionIdValid(const char* transactionId) const {
    if (!isTextValid(transactionId)) {
        return false;
    }

    const size_t length = strlen(transactionId);

    if (length < 3) return false;
    if (length >= 48) return false;

    return true;
}

void CommandValidator::setResult(
    ValidationResult& result,
    bool valid,
    const char* errorCode,
    const char* message,
    uint8_t pulseCount
) const {
    result.valid = valid;
    result.errorCode = errorCode;
    result.message = message;
    result.pulseCount = pulseCount;
}