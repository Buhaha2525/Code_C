// src/transaction/TransactionManager.h

#pragma once

#include <Arduino.h>
#include "../hardware/Dispenser.h"
#include "TransactionStore.h"

/*
   =========================================================
   MODULE : TransactionManager

   Rôle :
   - Gérer une transaction locale
   - Éviter plusieurs distributions simultanées
   - Relier une demande de paiement/distribution au Dispenser
   - Préparer la future intégration backend/MQTT
   =========================================================
*/

class TransactionManager {
public:
    enum class State {
        IDLE,
        CREATED,
        DISPENSING,
        SUCCESS,
        FAILED
    };

    TransactionManager();

   void begin(Dispenser* dispenser, TransactionStore* transactionStore);

    bool startTransaction(
        uint16_t amountFcfa,
        const char* transactionId,
        const char* source
    );

    void update();

    bool isBusy() const;
    bool hasSucceeded();
    bool hasFailed();

    State getState() const;

    uint16_t getCurrentAmountFcfa() const;
    const char* getCurrentTransactionId() const;
    const char* getCurrentSource() const;

    uint16_t getLastCompletedAmountFcfa() const;
    const char* getLastCompletedTransactionId() const;
    const char* getLastCompletedSource() const;

private:
    static constexpr size_t TRANSACTION_ID_SIZE = 48;
    static constexpr size_t SOURCE_SIZE = 32;

    Dispenser* _dispenser;

    TransactionStore* _transactionStore;

    State _state;

    uint16_t _currentAmountFcfa;

    char _currentTransactionId[TRANSACTION_ID_SIZE];
    char _currentSource[SOURCE_SIZE];

    uint16_t _lastCompletedAmountFcfa;

    char _lastCompletedTransactionId[TRANSACTION_ID_SIZE];
    char _lastCompletedSource[SOURCE_SIZE];

    bool _successFlag;
    bool _failureFlag;


    void resetCurrentTransaction();
    void copySafe(char* destination, size_t destinationSize, const char* source);
};