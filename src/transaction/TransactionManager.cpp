// src/transaction/TransactionManager.cpp

#include "TransactionManager.h"

TransactionManager::TransactionManager()
    : _dispenser(nullptr),
      _transactionStore(nullptr),
      _state(State::IDLE),
      _currentAmountFcfa(0),
      _lastCompletedAmountFcfa(0),
      _successFlag(false),
      _failureFlag(false) {
    _currentTransactionId[0] = '\0';
    _currentSource[0] = '\0';

    _lastCompletedTransactionId[0] = '\0';
    _lastCompletedSource[0] = '\0';
}

void TransactionManager::begin(
    Dispenser* dispenser,
    TransactionStore* transactionStore
    ) {
    _dispenser = dispenser;
    _transactionStore = transactionStore;

    resetCurrentTransaction();

    _state = State::IDLE;
    _successFlag = false;
    _failureFlag = false;

    Serial.println("[TransactionManager] Module initialisé.");
}

bool TransactionManager::startTransaction(
    uint16_t amountFcfa,
    const char* transactionId,
    const char* source
) {
    if (_dispenser == nullptr) {
        Serial.println("[TransactionManager][ERREUR] Dispenser non initialisé.");
        _state = State::FAILED;
        _failureFlag = true;
        return false;
    }

    if (isBusy()) {
        Serial.println("[TransactionManager][REFUS] Une transaction est déjà en cours.");
        return false;
    }

    if (amountFcfa == 0) {
        Serial.println("[TransactionManager][REFUS] Montant invalide.");
        _state = State::FAILED;
        _failureFlag = true;
        return false;
    }

    _currentAmountFcfa = amountFcfa;
    copySafe(_currentTransactionId, TRANSACTION_ID_SIZE, transactionId);
    copySafe(_currentSource, SOURCE_SIZE, source);

    _successFlag = false;
    _failureFlag = false;

    _state = State::CREATED;
    if (_transactionStore != nullptr) {
    _transactionStore->saveTransaction(
        _currentTransactionId,
        _currentAmountFcfa,
        _currentSource,
        TransactionStore::Status::CREATED
    );
    }


    Serial.println("----------------------------------------------");
    Serial.println("[TransactionManager] Nouvelle transaction créée.");

    Serial.print("[TransactionManager] Transaction ID : ");
    Serial.println(_currentTransactionId);

    Serial.print("[TransactionManager] Source : ");
    Serial.println(_currentSource);

    Serial.print("[TransactionManager] Montant : ");
    Serial.print(_currentAmountFcfa);
    Serial.println(" FCFA");
    Serial.println("----------------------------------------------");

    if (!_dispenser->dispenseAmount(_currentAmountFcfa, _currentSource)) {
    Serial.println("[TransactionManager][ERREUR] Le Dispenser a refusé la distribution.");

    _state = State::FAILED;
    _failureFlag = true;

    if (_transactionStore != nullptr) {
        _transactionStore->updateStatus(TransactionStore::Status::FAILED);
    }

    return false;
   }

   if (_transactionStore != nullptr) {
    _transactionStore->updateStatus(TransactionStore::Status::DISPENSING);
   }

   _state = State::DISPENSING;
    return true;
}

void TransactionManager::update() {
    if (_dispenser == nullptr) {
        return;
    }

    _dispenser->update();

    /*
       Si aucune transaction n'est en cours de distribution,
       on ne doit pas analyser les résultats du Dispenser.
    */
    if (_state != State::DISPENSING) {
        return;
    }

    /*
       Cas 1 : distribution réussie
    */
    if (_dispenser->hasCompleted()) {
    copySafe(
        _lastCompletedTransactionId,
        TRANSACTION_ID_SIZE,
        _currentTransactionId
    );

    _lastCompletedAmountFcfa = _currentAmountFcfa;

    copySafe(
        _lastCompletedSource,
        SOURCE_SIZE,
        _currentSource
    );

    _state = State::SUCCESS;
    _successFlag = true;

    if (_transactionStore != nullptr) {
        _transactionStore->updateStatus(TransactionStore::Status::SUCCESS);
    }

        Serial.println("----------------------------------------------");
        Serial.println("[TransactionManager] Transaction terminée avec succès.");

        Serial.print("[TransactionManager] Transaction ID : ");
        Serial.println(_currentTransactionId);

        Serial.print("[TransactionManager] Montant distribué : ");
        Serial.print(_currentAmountFcfa);
        Serial.println(" FCFA");

        Serial.println("----------------------------------------------");

        return;
    }

    /*
       Cas 2 : distribution échouée
       Important : ce bloc ne doit être testé qu'après le cas SUCCESS.
    */
    if (_dispenser->hasFailed()) {
        _state = State::FAILED;
        _failureFlag = true;

        if (_transactionStore != nullptr) {
            _transactionStore->updateStatus(TransactionStore::Status::FAILED);
        }

        Serial.println("----------------------------------------------");
        Serial.println("[TransactionManager][ERREUR] Transaction échouée.");

        Serial.print("[TransactionManager] Transaction ID : ");
        Serial.println(_currentTransactionId);

        Serial.print("[TransactionManager] Montant demandé : ");
        Serial.print(_currentAmountFcfa);
        Serial.println(" FCFA");

        Serial.println("----------------------------------------------");

        return;
    }
}

bool TransactionManager::isBusy() const {
    return _state == State::CREATED || _state == State::DISPENSING;
}

bool TransactionManager::hasSucceeded() {
    if (_successFlag) {
        _successFlag = false;
        resetCurrentTransaction();
        _state = State::IDLE;
        return true;
    }

    return false;
}

bool TransactionManager::hasFailed() {
    if (_failureFlag) {
        _failureFlag = false;
        resetCurrentTransaction();
        _state = State::IDLE;
        return true;
    }

    return false;
}

TransactionManager::State TransactionManager::getState() const {
    return _state;
}

uint16_t TransactionManager::getCurrentAmountFcfa() const {
    return _currentAmountFcfa;
}

const char* TransactionManager::getCurrentTransactionId() const {
    return _currentTransactionId;
}

const char* TransactionManager::getCurrentSource() const {
    return _currentSource;
}

uint16_t TransactionManager::getLastCompletedAmountFcfa() const {
    return _lastCompletedAmountFcfa;
}

const char* TransactionManager::getLastCompletedTransactionId() const {
    return _lastCompletedTransactionId;
}

const char* TransactionManager::getLastCompletedSource() const {
    return _lastCompletedSource;
}

void TransactionManager::resetCurrentTransaction() {
    _currentAmountFcfa = 0;
    _currentTransactionId[0] = '\0';
    _currentSource[0] = '\0';
}

void TransactionManager::copySafe(
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