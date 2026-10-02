// src/config/config.h

#pragma once

#include <Arduino.h>

static constexpr uint32_t COIN_END_TIMEOUT_MS = 1000;

namespace AppConfig {

    /*
       =====================================================
       IDENTITÉ MACHINE (version + OTA)
       =====================================================
       ⚠️ L'ID machine est maintenant dynamique (voir MachineIdentity.h)
    */
    namespace Machine {
        static constexpr const char* FIRMWARE_VERSION = "0.0.4";

        const String OTA_VERSION_URL =
            "https://raw.githubusercontent.com/Buhaha2525/OTA_bin/main/version.txt";

        const String OTA_BINARY_BASE_URL =
            "https://github.com/Buhaha2525/OTA_bin/releases/download/";
    }

    /*
       =====================================================
       CONFIGURATION OTA
       =====================================================
    */
    namespace Ota {
        static constexpr uint32_t CHECK_INTERVAL_MS = 2UL * 60UL * 1000UL;
        static constexpr uint32_t INITIAL_DELAY_MS = 30UL * 1000UL;
    }

    /*
       =====================================================
       CONFIGURATION HARDWARE
       =====================================================
    */
    namespace Pins {
        static constexpr uint8_t COIN_INPUT_PIN          = 27;
        static constexpr uint8_t LED_STATUS_PIN          = 26;
        static constexpr uint8_t PULSE_OUT_PIN           = 25;
        static constexpr uint8_t MACHINE_AVAILABLE_PIN   = 33;
    }

    namespace Debug {
        static constexpr bool COIN_INPUT_RAW_LOG_ENABLED = false;
        static constexpr uint32_t COIN_INPUT_RAW_LOG_INTERVAL_MS = 2000;
    }

    /*
       =====================================================
       CONFIGURATION MONÉTAIRE / IMPULSIONS
       =====================================================
    */
    struct Tariff {
        uint16_t amountFcfa;
        uint8_t pulses;
        const char* label;
    };

    static constexpr Tariff TARIFFS[] = {
        {50,  1,  "50 FCFA"},
        {100, 2,  "100 FCFA"},
        {200, 4,  "200 FCFA"},
        {250, 5,  "250 FCFA"},
        {500, 10, "500 FCFA"}
    };

    static constexpr uint8_t TARIFF_COUNT =
        sizeof(TARIFFS) / sizeof(TARIFFS[0]);

    namespace Money {
        static constexpr uint16_t PULSE_VALUE_FCFA = 50;

        static constexpr uint16_t MIN_AMOUNT_FCFA = 50;
        static constexpr uint16_t MAX_AMOUNT_FCFA = 5700;

        static constexpr uint8_t MAX_OUTPUT_PULSES =
            MAX_AMOUNT_FCFA / PULSE_VALUE_FCFA;

        static constexpr bool isAmountValid(uint16_t amountFcfa) {
            return amountFcfa >= MIN_AMOUNT_FCFA
                && amountFcfa <= MAX_AMOUNT_FCFA
                && amountFcfa % PULSE_VALUE_FCFA == 0;
        }

        static constexpr uint8_t amountToPulseCount(uint16_t amountFcfa) {
            return isAmountValid(amountFcfa)
                ? static_cast<uint8_t>(amountFcfa / PULSE_VALUE_FCFA)
                : 0;
        }

        static constexpr uint16_t pulseCountToAmount(uint16_t pulseCount) {
            return pulseCount > 0 && pulseCount <= MAX_OUTPUT_PULSES
                ? static_cast<uint16_t>(pulseCount * PULSE_VALUE_FCFA)
                : 0;
        }
    }

    /*
       =====================================================
       TIMING / DÉLAIS
       =====================================================
    */
    namespace Timing {
        static constexpr uint32_t COIN_DEBOUNCE_US = 50000;
        static constexpr uint32_t COIN_END_TIMEOUT_MS = 1000;
        static constexpr uint16_t PULSE_HIGH_MS = 100;
        static constexpr uint16_t PULSE_LOW_MS = 100;
        static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 15000;
        static constexpr uint32_t WIFI_RETRY_DELAY_MS = 5000;
        static constexpr uint16_t MQTT_RETRY_DELAY_MS = 5000;
    }

    /*
       =====================================================
       LIMITES SYSTÈME
       =====================================================
    */
    namespace Limits {
        static constexpr uint16_t MQTT_PAYLOAD_MAX_SIZE = 1024;
        static constexpr uint16_t TELEMETRY_JSON_SIZE   = 512;
        static constexpr uint16_t COMMAND_JSON_SIZE     = 256;
        static constexpr uint16_t MAX_ALLOWED_AMOUNT_FCFA = Money::MAX_AMOUNT_FCFA;
        static constexpr uint8_t MAX_OUTPUT_PULSES = Money::MAX_OUTPUT_PULSES;
    }

    /*
       =====================================================
       ÉTATS SYSTÈME
       =====================================================
    */
    enum class SystemState {
        BOOT,
        WIFI_CONNECTING,
        MQTT_CONNECTING,
        IDLE,
        WAITING_FOR_COIN,
        COIN_RECEIVED,
        VALIDATING_COIN,
        DISPENSING,
        DISPENSE_SUCCESS,
        DISPENSE_FAILED,
        ERROR,
        MAINTENANCE
    };

    namespace Security {
        static constexpr bool MQTT_USE_TLS = false;
        static constexpr bool MQTT_USE_INSECURE_TLS_FOR_TEST = true;
    }
}