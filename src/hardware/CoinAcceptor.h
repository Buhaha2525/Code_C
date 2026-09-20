// src/hardware/CoinAcceptor.h

#pragma once

#include <Arduino.h>
#include "../config/config.h"

/*
   =========================================================
   MODULE : CoinAcceptor
   Rôle :
   - Lire les impulsions venant du monnayeur physique
   - Compter les impulsions via interruption
   - Détecter quand la séquence d'impulsions est terminée
   - Convertir le nombre d'impulsions en montant FCFA
   =========================================================
*/

class CoinAcceptor {
public:
    explicit CoinAcceptor(uint8_t inputPin = AppConfig::Pins::COIN_INPUT_PIN);

    void begin();
    void update();

    bool hasCoinEvent();

    uint16_t getLastAmountFcfa() const;
    uint16_t getLastPulseCount() const;

    void reset();
    void enable();
    void disable();

private:
    uint8_t _inputPin;

    volatile uint16_t _pulseCount;
    volatile uint32_t _lastPulseUs;
    volatile bool _pulseDetected;

    volatile bool _enabled;
    bool _eventReady;

    uint16_t _lastCompletedPulseCount;
    uint16_t _lastAmountFcfa;

    static CoinAcceptor* _instance;

    static void IRAM_ATTR handleInterruptStatic();
    void IRAM_ATTR handleInterrupt();

    uint16_t convertPulsesToAmount(uint16_t pulseCount) const;
};