// src/telemetry/OtaService.h

#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include "config/config.h"

class OtaService {
public:
    OtaService() = default;

    // =====================================================
    // À APPELER DANS loop() À CHAQUE ITÉRATION
    // Gère automatiquement :
    //  - le délai initial après boot
    //  - la périodicité des checks
    //  - la lecture de version.txt
    //  - la construction de l'URL du binaire
    //  - la comparaison version locale / distante
    //  - le flash + reboot si nécessaire
    // =====================================================
    void update() {
        if (WiFi.status() != WL_CONNECTED) return;

        const uint32_t now = millis();

        // Délai initial après boot
        if (!initialDelayElapsed) {
            if (now < AppConfig::Ota::INITIAL_DELAY_MS) return;
            initialDelayElapsed = true;
            lastCheckMs = now - AppConfig::Ota::CHECK_INTERVAL_MS;
        }

        // Périodicité
        if (now - lastCheckMs < AppConfig::Ota::CHECK_INTERVAL_MS) return;

        lastCheckMs = now;

        Serial.println();
        Serial.println("[OTA][AUTO] ⏰ Check automatique déclenché.");
        checkAndPerformUpdate();
    }

    // =====================================================
    // Check + flash (peut être appelé manuellement aussi)
    // =====================================================
    void checkAndPerformUpdate() {
        Serial.println("[OTA] ========== DEBUT CHECK OTA ==========");

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[OTA] ❌ Wi-Fi non connecté.");
            return;
        }

        // --- ÉTAPE 1 : lire version.txt ---
        String latestVersion = fetchRemoteVersion();
        if (latestVersion.length() == 0) {
            Serial.println("[OTA] ❌ Impossible de lire version.txt");
            Serial.println("[OTA] ========== FIN CHECK OTA ==========");
            return;
        }

        Serial.print("[OTA] Version locale   : '");
        Serial.print(AppConfig::Machine::FIRMWARE_VERSION);
        Serial.println("'");
        Serial.print("[OTA] Version distante : '");
        Serial.print(latestVersion);
        Serial.println("'");

        // --- ÉTAPE 2 : comparer ---
        if (latestVersion == AppConfig::Machine::FIRMWARE_VERSION) {
            Serial.println("[OTA] ✅ Déjà à jour. Rien à faire.");
            Serial.println("[OTA] ========== FIN CHECK OTA ==========");
            return;
        }

        Serial.println("[OTA] 🚀 Nouvelle version détectée → flash...");

        // --- ÉTAPE 3 : construire l'URL du binaire ---
        String binaryUrl = buildBinaryUrl(latestVersion);
        Serial.print("[OTA] URL binaire : ");
        Serial.println(binaryUrl);

        // --- ÉTAPE 4 : flasher ---
        performUpdate(binaryUrl);

        Serial.println("[OTA] ========== FIN CHECK OTA ==========");
    }

    // Construit l'URL : BASE + "v{VERSION}/firmware_{VERSION}.bin"
    // Ex: https://github.com/Buhaha2525/OTA_bin/releases/download/v0.3.0/firmware_0.3.0.bin
    String buildBinaryUrl(const String& version) {
        String url = AppConfig::Machine::OTA_BINARY_BASE_URL;
        url += "v";
        url += version;
        url += "/firmware_";
        url += version;
        url += ".bin";
        return url;
    }

private:
    bool     initialDelayElapsed = false;
    uint32_t lastCheckMs         = 0;

    // =====================================================
    // Récupère la version depuis version.txt
    // =====================================================
    String fetchRemoteVersion() {
        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(15000);

        HTTPClient http;
        if (!http.begin(client, AppConfig::Machine::OTA_VERSION_URL)) {
            Serial.println("[OTA] ❌ http.begin versionUrl échoué.");
            return "";
        }

        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.setTimeout(10000);

        int httpCode = http.GET();
        Serial.print("[OTA] Code HTTP version : ");
        Serial.println(httpCode);

        if (httpCode != HTTP_CODE_OK) {
            http.end();
            return "";
        }

        String latestVersion = http.getString();
        http.end();

        // --- Nettoyage agressif ---
        latestVersion.trim();

        // 1ère ligne uniquement (protection multi-lignes)
        int nl = latestVersion.indexOf('\n');
        if (nl >= 0) {
            Serial.println("[OTA] ⚠️ Multi-lignes détecté, troncature.");
            latestVersion = latestVersion.substring(0, nl);
            latestVersion.trim();
        }

        // BOM UTF-8
        if (latestVersion.length() >= 3
            && (uint8_t)latestVersion[0] == 0xEF
            && (uint8_t)latestVersion[1] == 0xBB
            && (uint8_t)latestVersion[2] == 0xBF) {
            Serial.println("[OTA] ⚠️ BOM UTF-8 détecté, suppression.");
            latestVersion.remove(0, 3);
            latestVersion.trim();
        }

        // ASCII imprimable uniquement
        String cleaned;
        for (size_t i = 0; i < latestVersion.length(); i++) {
            char c = latestVersion[i];
            if (c >= 0x20 && c <= 0x7E) cleaned += c;
        }
        latestVersion = cleaned;
        latestVersion.trim();

        // --- Validations ---
        if (latestVersion.length() == 0) {
            Serial.println("[OTA] ❌ version.txt vide.");
            return "";
        }

        if (latestVersion.length() >= 15) {
            Serial.println("[OTA] ❌ version.txt trop long (HTML ?).");
            return "";
        }

        if (latestVersion.indexOf('<') >= 0 ||
            latestVersion.indexOf("DOCTYPE") >= 0) {
            Serial.println("[OTA] ❌ HTML détecté dans version.txt.");
            return "";
        }

        return latestVersion;
    }

    // =====================================================
    // Télécharge et flashe le binaire
    // =====================================================
    void performUpdate(const String& binaryUrl) {
        WiFiClientSecure updateClient;
        updateClient.setInsecure();
        updateClient.setTimeout(20000);

        httpUpdate.onStart([]() {
            Serial.println("[OTA] Début du téléversement...");
        });

        httpUpdate.onEnd([]() {
            Serial.println();
            Serial.println("[OTA] Téléversement terminé.");
        });

        httpUpdate.onProgress([](int current, int total) {
            static int lastPercent = -1;
            if (total > 0) {
                int percent = (current * 100) / total;
                if (percent != lastPercent) {
                    lastPercent = percent;
                    Serial.printf("\r[OTA] Progression : %d%% (%d / %d octets)",
                                  percent, current, total);
                    Serial.flush();
                }
            }
        });

        httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        httpUpdate.rebootOnUpdate(true);

        HTTPUpdateResult ret = httpUpdate.update(updateClient, binaryUrl);

        Serial.println();
        switch (ret) {
            case HTTP_UPDATE_FAILED:
                Serial.printf("[OTA] ❌ FAILED (err %d): %s\n",
                              httpUpdate.getLastError(),
                              httpUpdate.getLastErrorString().c_str());
                break;

            case HTTP_UPDATE_NO_UPDATES:
                Serial.println("[OTA] ℹ️ NO_UPDATES");
                break;

            case HTTP_UPDATE_OK:
                Serial.println("[OTA] ✅ OK → redémarrage imminent...");
                break;
        }
    }
};

#endif // OTA_SERVICE_H