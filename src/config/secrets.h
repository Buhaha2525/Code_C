// src/config/secrets.h

#pragma once

namespace AppSecrets {

    namespace WiFiConfig {
        //static constexpr const char* SSID = "4G UFI-6A77";
        static constexpr const char* SSID = "Diagne famillly ";
        static constexpr const char* PASSWORD = "Diagne2006";
    }

    namespace MqttConfig {
        static constexpr const char* HOST = "161.97.112.111";
        static constexpr uint16_t PORT = 1883;
        // ⚠️ CLIENT_ID supprimé → devient dynamique (= ID machine)
        static constexpr const char* USERNAME = "";
        static constexpr const char* PASSWORD = "";
        static constexpr bool USE_TLS = false;
    }

    namespace Certificates {
        static constexpr const char* ROOT_CA = "";
    }
}