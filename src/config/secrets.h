// src/config/secrets.h

#pragma once

namespace AppSecrets {

    namespace WiFiConfig {
#if defined(GROUP_BISRIO)
        // WiFi du groupe BISRIO
        static constexpr const char* SSID = "Diagne famillly ";
        static constexpr const char* PASSWORD = "Diagne2006";

#elif defined(GROUP_OXEWEUL)
        // WiFi du groupe OXEWEUL
        static constexpr const char* SSID = "Diagne famillly ";
        static constexpr const char* PASSWORD = "Diagne2006";

#else
#error "Aucun groupe défini"
#endif
    }

    namespace MqttConfig {
        // =====================================================
        // ✅ MQTT DIFFÉRENCIÉ PAR GROUPE
        // =====================================================
#if defined(GROUP_BISRIO)
        // Serveur MQTT BISRIO
        static constexpr const char* HOST = "161.97.112.111";
        static constexpr uint16_t PORT = 1883;

#elif defined(GROUP_OXEWEUL)
        // Serveur MQTT OXEWEUL
        static constexpr const char* HOST = "161.97.112.111";     // ← Remplace par ton IP
        static constexpr uint16_t PORT = 1883;

#else
#error "Aucun groupe défini"
#endif

        static constexpr const char* USERNAME = "";
        static constexpr const char* PASSWORD = "";
        static constexpr bool USE_TLS = false;
    }

    namespace Certificates {
        static constexpr const char* ROOT_CA = "";
    }
}