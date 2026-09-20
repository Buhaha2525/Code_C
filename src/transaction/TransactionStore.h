// src/transaction/TransactionStore.h

#pragma once

#include <Arduino.h>
#include <Preferences.h>

/*
   =========================================================
   MODULE : TransactionStore

   Rôle :
   - Stocker localement la dernière transaction importante
   - Survivre à un redémarrage de l'ESP32
   - Préparer la gestion des coupures électriques
   - Préparer la prévention des doubles distributions
   =========================================================
*/

class TransactionStore {
public:
    enum class Status : uint8_t {
        EMPTY = 0,
        CREATED = 1,
        DISPENSING = 2,
        SUCCESS = 3,
        FAILED = 4
    };

    struct __attribute__((packed)) Record {
        char transactionId[48];
        char source[24];
        uint16_t amountFcfa;
        uint16_t pulseCount;
        Status status;
        uint32_t createdAtMs;
        uint32_t updatedAtMs;
        uint32_t syncCount;
    };

    TransactionStore();

    bool begin();

    bool saveTransaction(
        const char* transactionId,
        uint16_t amountFcfa,
        const char* source,
        Status status
    );

    bool enqueueOfflineTransaction(
        const char* transactionId,
        uint16_t amountFcfa,
        const char* source,
        Status status,
        uint16_t pulseCount = 0
    );

    bool syncPendingTransactions(
        bool (*publisher)(const char* topic, const char* payload),
        const char* topic
    );

    bool updateStatus(Status status);

    bool loadLastTransaction(Record& record);
    bool hasStoredTransaction() const;
    bool hasPendingTransaction() const;
    bool hasPendingSync() const;
    size_t getPendingCount() const;

    bool clear();

    void printLastTransaction();

    static const char* statusToString(Status status);

private:
    static constexpr size_t MAX_PENDING_RECORDS = 50;

    Preferences _preferences;

    bool _ready;
    bool _hasRecord;
    size_t _pendingCount;

    Record _lastRecord;
    Record _pendingRecords[MAX_PENDING_RECORDS];

    void resetLocalRecord();
    void resetPendingQueue();
    void loadFromPreferences();
    bool writeToPreferences();
    bool loadPendingQueueFromPreferences();
    bool writePendingQueueToPreferences();

    void copySafe(
        char* destination,
        size_t destinationSize,
        const char* source
    );
};