// src/transaction/TransactionStore.cpp

#include "TransactionStore.h"
#include "../config/config.h"
#include "../config/MachineIdentity.h"
#include <ArduinoJson.h>
#include <WiFi.h>

TransactionStore::TransactionStore()
    : _ready(false),
      _hasRecord(false),
      _pendingCount(0) {
    resetLocalRecord();
    resetPendingQueue();
}

bool TransactionStore::begin() {
    _ready = _preferences.begin("tx_store", false);

    if (!_ready) {
        Serial.println("[TransactionStore][ERREUR] Impossible d'ouvrir Preferences.");
        return false;
    }

    loadFromPreferences();
    loadPendingQueueFromPreferences();

    Serial.println("[TransactionStore] Module initialisé.");

    if (_hasRecord) {
        printLastTransaction();
    } else {
        Serial.println("[TransactionStore] Aucune transaction locale enregistrée.");
    }

    if (_pendingCount > 0) {
        Serial.print("[TransactionStore] Transactions en attente de synchronisation : ");
        Serial.println(_pendingCount);
    }

    return true;
}

bool TransactionStore::saveTransaction(
    const char* transactionId,
    uint16_t amountFcfa,
    const char* source,
    Status status
) {
    if (!_ready) {
        Serial.println("[TransactionStore][ERREUR] Store non initialisé.");
        return false;
    }

    copySafe(_lastRecord.transactionId, sizeof(_lastRecord.transactionId), transactionId);
    copySafe(_lastRecord.source, sizeof(_lastRecord.source), source);

    _lastRecord.amountFcfa = amountFcfa;
    _lastRecord.pulseCount = AppConfig::Money::amountToPulseCount(amountFcfa);
    _lastRecord.status = status;
    _lastRecord.createdAtMs = millis();
    _lastRecord.updatedAtMs = _lastRecord.createdAtMs;
    _lastRecord.syncCount = 0;

    _hasRecord = true;

    if (!writeToPreferences()) {
        Serial.println("[TransactionStore][ERREUR] Échec sauvegarde transaction.");
        return false;
    }

    Serial.println("[TransactionStore] Transaction sauvegardée localement.");
    Serial.print("[TransactionStore] Transaction ID : ");
    Serial.println(_lastRecord.transactionId);
    Serial.print("[TransactionStore] Montant : ");
    Serial.print(_lastRecord.amountFcfa);
    Serial.println(" FCFA");
    Serial.print("[TransactionStore] Statut : ");
    Serial.println(statusToString(_lastRecord.status));

    return true;
}

bool TransactionStore::enqueueOfflineTransaction(
    const char* transactionId,
    uint16_t amountFcfa,
    const char* source,
    Status status,
    uint16_t pulseCount
) {
    if (!_ready) {
        Serial.println("[TransactionStore][ERREUR] Store non initialisé pour file locale.");
        return false;
    }

    if (transactionId == nullptr || source == nullptr) {
        Serial.println("[TransactionStore][WARN] Transaction locale invalide, enregistrement ignoré.");
        return false;
    }

    if (_pendingCount >= MAX_PENDING_RECORDS) {
        Serial.println("[TransactionStore][WARN] File locale pleine, suppression de l'entrée la plus ancienne.");
        for (size_t i = 1; i < _pendingCount; ++i) {
            _pendingRecords[i - 1] = _pendingRecords[i];
        }
        _pendingCount--;
    }

    Record& record = _pendingRecords[_pendingCount];
    memset(&record, 0, sizeof(record));

    copySafe(record.transactionId, sizeof(record.transactionId), transactionId);
    copySafe(record.source, sizeof(record.source), source);
    record.amountFcfa = amountFcfa;
    record.pulseCount = (pulseCount > 0) ? pulseCount : AppConfig::Money::amountToPulseCount(amountFcfa);
    record.status = status;
    record.createdAtMs = millis();
    record.updatedAtMs = record.createdAtMs;
    record.syncCount = 0;

    _pendingCount++;

    if (!writePendingQueueToPreferences()) {
        Serial.println("[TransactionStore][ERREUR] Échec écriture file locale de synchronisation.");
        _pendingCount--;
        return false;
    }

    Serial.print("[TransactionStore] Transaction en file locale (pending=");
    Serial.print(_pendingCount);
    Serial.println(").");

    return true;
}

bool TransactionStore::syncPendingTransactions(
    bool (*publisher)(const char* topic, const char* payload),
    const char* topic
) {
    if (!_ready || publisher == nullptr || topic == nullptr || _pendingCount == 0) {
        return false;
    }

    bool syncedAny = false;

    for (size_t index = 0; index < _pendingCount;) {
        Record& record = _pendingRecords[index];

        if (record.transactionId[0] == '\0') {
            index++;
            continue;
        }

        const uint16_t pulses = (record.pulseCount > 0)
            ? record.pulseCount
            : AppConfig::Money::amountToPulseCount(record.amountFcfa);

        JsonDocument doc;
        doc["type"] = "coin_payment_detected";
        doc["eventType"] = "physical_coin_payment";
        doc["machineId"] = machineIdentity.getId();
        doc["amountFcfa"] = record.amountFcfa;
        doc["amount"] = record.amountFcfa;               // ✅ AJOUT
        doc["pulseCount"] = pulses;
        doc["paymentMethod"] = "coin";                    // ✅ AJOUT
        doc["source"] = "physical_coin";
        doc["status"] = "DETECTED";
        doc["eventId"] = record.transactionId;
        doc["transactionId"] = record.transactionId;
        doc["offlineSync"] = true;
        doc["uptimeMs"] = millis();
        doc["createdAtMs"] = record.createdAtMs;
        doc["updatedAtMs"] = record.updatedAtMs;
        doc["syncCount"] = record.syncCount;
        doc["macAddress"] = WiFi.macAddress();
        char payload[AppConfig::Limits::TELEMETRY_JSON_SIZE];
        const size_t length = serializeJson(doc, payload, sizeof(payload));

        if (length == 0 || length >= sizeof(payload)) {
            Serial.println("[TransactionStore][WARN] Payload de synchronisation invalide.");
            index++;
            continue;
        }

        if (!publisher(topic, payload)) {
            Serial.println("[TransactionStore][WARN] Synchronisation MQTT reportée, file locale conservée.");
            break;
        }

        Serial.print("[TransactionStore] Transaction hors ligne synchronisée avec succès via MQTT (Event ID: ");
        Serial.print(record.transactionId);
        Serial.println(").");

        // Retirer l'élément synchronisé de la file locale FIFO
        for (size_t j = index + 1; j < _pendingCount; ++j) {
            _pendingRecords[j - 1] = _pendingRecords[j];
        }
        _pendingCount--;
        syncedAny = true;

        if (!writePendingQueueToPreferences()) {
            Serial.println("[TransactionStore][ERREUR] Échec mise à jour file locale après synchronisation.");
        }
    }

    return syncedAny;
}

bool TransactionStore::updateStatus(Status status) {
    if (!_ready) {
        Serial.println("[TransactionStore][ERREUR] Store non initialisé.");
        return false;
    }

    if (!_hasRecord) {
        Serial.println("[TransactionStore][WARN] Aucun enregistrement à mettre à jour.");
        return false;
    }

    _lastRecord.status = status;
    _lastRecord.updatedAtMs = millis();

    if (!writeToPreferences()) {
        Serial.println("[TransactionStore][ERREUR] Échec mise à jour statut.");
        return false;
    }

    Serial.print("[TransactionStore] Statut mis à jour : ");
    Serial.println(statusToString(status));

    return true;
}

bool TransactionStore::loadLastTransaction(Record& record) {
    if (!_hasRecord) {
        return false;
    }

    record = _lastRecord;
    return true;
}

bool TransactionStore::hasStoredTransaction() const {
    return _hasRecord;
}

bool TransactionStore::hasPendingTransaction() const {
    if (!_hasRecord) {
        return false;
    }

    return _lastRecord.status == Status::CREATED ||
           _lastRecord.status == Status::DISPENSING;
}

bool TransactionStore::hasPendingSync() const {
    return _pendingCount > 0;
}

size_t TransactionStore::getPendingCount() const {
    return _pendingCount;
}

bool TransactionStore::clear() {
    if (!_ready) {
        return false;
    }

    _preferences.clear();
    resetLocalRecord();
    resetPendingQueue();

    Serial.println("[TransactionStore] Mémoire transactionnelle effacée.");

    return true;
}

void TransactionStore::printLastTransaction() {
    if (!_hasRecord) {
        Serial.println("[TransactionStore] Aucune transaction à afficher.");
        return;
    }

    Serial.println("----------------------------------------------");
    Serial.println("[TransactionStore] Dernière transaction locale :");
    Serial.print("Transaction ID : ");
    Serial.println(_lastRecord.transactionId);
    Serial.print("Source         : ");
    Serial.println(_lastRecord.source);
    Serial.print("Montant        : ");
    Serial.print(_lastRecord.amountFcfa);
    Serial.println(" FCFA");
    Serial.print("Statut         : ");
    Serial.println(statusToString(_lastRecord.status));
    Serial.print("Updated at ms  : ");
    Serial.println(_lastRecord.updatedAtMs);
    Serial.println("----------------------------------------------");
}

const char* TransactionStore::statusToString(Status status) {
    switch (status) {
        case Status::EMPTY:
            return "EMPTY";
        case Status::CREATED:
            return "CREATED";
        case Status::DISPENSING:
            return "DISPENSING";
        case Status::SUCCESS:
            return "SUCCESS";
        case Status::FAILED:
            return "FAILED";
        default:
            return "UNKNOWN";
    }
}

void TransactionStore::resetLocalRecord() {
    _lastRecord.transactionId[0] = '\0';
    _lastRecord.source[0] = '\0';
    _lastRecord.amountFcfa = 0;
    _lastRecord.status = Status::EMPTY;
    _lastRecord.createdAtMs = 0;
    _lastRecord.updatedAtMs = 0;
    _lastRecord.syncCount = 0;
    _hasRecord = false;
}

void TransactionStore::resetPendingQueue() {
    for (size_t i = 0; i < MAX_PENDING_RECORDS; ++i) {
        memset(&_pendingRecords[i], 0, sizeof(_pendingRecords[i]));
    }
    _pendingCount = 0;
}

void TransactionStore::loadFromPreferences() {
    _hasRecord = _preferences.getBool("has", false);

    if (!_hasRecord) {
        resetLocalRecord();
        return;
    }

    String transactionId = _preferences.getString("txid", "");
    String source = _preferences.getString("src", "");

    copySafe(_lastRecord.transactionId, sizeof(_lastRecord.transactionId), transactionId.c_str());
    copySafe(_lastRecord.source, sizeof(_lastRecord.source), source.c_str());

    _lastRecord.amountFcfa = _preferences.getUInt("amount", 0);
    _lastRecord.status = static_cast<Status>(_preferences.getUChar("status", 0));
    _lastRecord.createdAtMs = _preferences.getULong("created", 0);
    _lastRecord.updatedAtMs = _preferences.getULong("updated", 0);
    _lastRecord.syncCount = _preferences.getULong("sync", 0);
}

bool TransactionStore::writeToPreferences() {
    if (!_ready) {
        return false;
    }

    _preferences.putBool("has", _hasRecord);
    _preferences.putString("txid", _lastRecord.transactionId);
    _preferences.putString("src", _lastRecord.source);
    _preferences.putUInt("amount", _lastRecord.amountFcfa);
    _preferences.putUChar("status", static_cast<uint8_t>(_lastRecord.status));
    _preferences.putULong("created", _lastRecord.createdAtMs);
    _preferences.putULong("updated", _lastRecord.updatedAtMs);
    _preferences.putULong("sync", _lastRecord.syncCount);

    return true;
}

bool TransactionStore::loadPendingQueueFromPreferences() {
    if (!_ready) {
        return false;
    }

    _pendingCount = _preferences.getUInt("queue_count", 0);
    if (_pendingCount == 0) {
        resetPendingQueue();
        return true;
    }

    if (_pendingCount > MAX_PENDING_RECORDS) {
        _pendingCount = MAX_PENDING_RECORDS;
    }

    const size_t bytesNeeded = _pendingCount * sizeof(Record);
    const size_t read = _preferences.getBytes("queue_blob", _pendingRecords, bytesNeeded);

    if (read != bytesNeeded) {
        Serial.println("[TransactionStore][WARN] File locale invalide, réinitialisation.");
        resetPendingQueue();
        _preferences.putUInt("queue_count", 0);
        return false;
    }

    return true;
}

bool TransactionStore::writePendingQueueToPreferences() {
    if (!_ready) {
        return false;
    }

    _preferences.putUInt("queue_count", static_cast<uint32_t>(_pendingCount));

    if (_pendingCount == 0) {
        _preferences.remove("queue_blob");
        return true;
    }

    const size_t bytesNeeded = _pendingCount * sizeof(Record);
    return _preferences.putBytes("queue_blob", _pendingRecords, bytesNeeded) == bytesNeeded;
}

void TransactionStore::copySafe(
    char* destination,
    size_t destinationSize,
    const char* source
) {
    if (destination == nullptr || destinationSize == 0) {
        return;
    }

    if (source == nullptr) {
        destination[0] = '\0';
        return;
    }

    strncpy(destination, source, destinationSize - 1);
    destination[destinationSize - 1] = '\0';
}