// src/telemetry/OtaService.h

#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include "config/config.h"
#include "telemetry/LogService.h"     // ✅ AJOUT pour remoteLog

class OtaService {
public:
    OtaService() = default;

    // =====================================================
    // À APPELER DANS loop() À CHAQUE ITÉRATION
    // =====================================================
    void update() {
        if (WiFi.status() != WL_CONNECTED) return;

        const uint32_t now = millis();

        if (!initialDelayElapsed) {
            if (now < AppConfig::Ota::INITIAL_DELAY_MS) return;
            initialDelayElapsed = true;
            lastCheckMs = now - AppConfig::Ota::CHECK_INTERVAL_MS;
        }

        if (now - lastCheckMs < AppConfig::Ota::CHECK_INTERVAL_MS) return;

        lastCheckMs = now;

        Serial.println();
        Serial.println("[OTA][AUTO] ⏰ Check automatique déclenché.");

        checkAndPerformUpdate();
    }

    // =====================================================
    // Check + flash
    // =====================================================
    void checkAndPerformUpdate() {
        Serial.println("[OTA] ========== DEBUT CHECK OTA ==========");
        remoteLog.info("OTA", "Check OTA demarre");                    // ✅ AJOUT

        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[OTA] ❌ Wi-Fi non connecté.");
            remoteLog.warn("OTA", "Wi-Fi non connecte");              // ✅ AJOUT
            return;
        }

        String latestVersion = fetchRemoteVersion();
        if (latestVersion.length() == 0) {
            Serial.println("[OTA] ❌ Impossible de lire version.txt");
            remoteLog.error("OTA", "Lecture version.txt echouee");    // ✅ AJOUT
            return;
        }

        Serial.print("[OTA] Version locale   : '");
        Serial.print(AppConfig::Machine::FIRMWARE_VERSION);
        Serial.println("'");
        Serial.print("[OTA] Version distante : '");
        Serial.print(latestVersion);
        Serial.println("'");

        if (latestVersion == AppConfig::Machine::FIRMWARE_VERSION) {
            Serial.println("[OTA] ✅ Déjà à jour. Rien à faire.");
            // ⚠️ Pas de log distant ici : trop fréquent (toutes les 2 min)
            return;
        }

        Serial.println("[OTA] 🚀 Nouvelle version détectée → flash...");

        // ✅ Log distant IMPORTANT : nouvelle version détectée
        char msg[100];
        snprintf(msg, sizeof(msg),
                 "Nouvelle version %s detectee (locale %s)",
                 latestVersion.c_str(),
                 AppConfig::Machine::FIRMWARE_VERSION);
        remoteLog.info("OTA", msg);

        String binaryUrl = buildBinaryUrl(latestVersion);
        Serial.print("[OTA] URL binaire : ");
        Serial.println(binaryUrl);

        remoteLog.info("OTA", binaryUrl.c_str());                     // ✅ AJOUT

        performUpdate(binaryUrl);

        Serial.println("[OTA] ========== FIN CHECK OTA ==========");
    }

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

    String fetchRemoteVersion() {
        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(15000);

        HTTPClient http;
        if (!http.begin(client, AppConfig::Machine::OTA_VERSION_URL)) {
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

        latestVersion.trim();

        int nl = latestVersion.indexOf('\n');
        if (nl >= 0) {
            latestVersion = latestVersion.substring(0, nl);
            latestVersion.trim();
        }

        if (latestVersion.length() >= 3
            && (uint8_t)latestVersion[0] == 0xEF
            && (uint8_t)latestVersion[1] == 0xBB
            && (uint8_t)latestVersion[2] == 0xBF) {
            latestVersion.remove(0, 3);
            latestVersion.trim();
        }

        String cleaned;
        for (size_t i = 0; i < latestVersion.length(); i++) {
            char c = latestVersion[i];
            if (c >= 0x20 && c <= 0x7E) cleaned += c;
        }
        latestVersion = cleaned;
        latestVersion.trim();

        if (latestVersion.length() == 0) return "";
        if (latestVersion.length() >= 15) return "";
        if (latestVersion.indexOf('<') >= 0) return "";
        if (latestVersion.indexOf("DOCTYPE") >= 0) return "";

        return latestVersion;
    }

    void performUpdate(const String& binaryUrl) {
        WiFiClientSecure updateClient;
        updateClient.setInsecure();
        updateClient.setTimeout(20000);

        httpUpdate.onStart([]() {
            Serial.println("[OTA] Début du téléversement...");
            remoteLog.info("OTA", "Telechargement demarre");          // ✅ AJOUT
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
            case HTTP_UPDATE_FAILED: {
                Serial.printf("[OTA] ❌ FAILED (err %d): %s\n",
                              httpUpdate.getLastError(),
                              httpUpdate.getLastErrorString().c_str());

                // ✅ Log distant de l'échec
                char errMsg[140];
                snprintf(errMsg, sizeof(errMsg),
                         "Flash echoue (err %d): %s",
                         httpUpdate.getLastError(),
                         httpUpdate.getLastErrorString().c_str());
                remoteLog.error("OTA", errMsg);
                break;
            }

            case HTTP_UPDATE_NO_UPDATES:
                Serial.println("[OTA] ℹ️ NO_UPDATES");
                remoteLog.info("OTA", "Aucune mise a jour");          // ✅ AJOUT
                break;

            case HTTP_UPDATE_OK:
                Serial.println("[OTA] ✅ OK → redémarrage imminent...");
                remoteLog.info("OTA", "Flash OK, redemarrage");       // ✅ AJOUT
                break;
        }
    }
};

#endif // OTA_SERVICE_H