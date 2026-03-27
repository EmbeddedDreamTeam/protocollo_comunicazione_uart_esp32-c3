#pragma once

#include <string>

class WifiManager {
public:
    /**
     * Avvia l'ESP32 in modalità Access Point.
     * Il Mac potrà connettersi alla rete Wi-Fi creata dall'ESP32.
     * L'IP dell'ESP32 sarà sempre: 192.168.4.1
     *
     * @param ssid      Nome della rete Wi-Fi (max 32 caratteri)
     * @param password  Password (min 8 caratteri, lascia "" per rete aperta)
     */
    static void init_ap(const std::string& ssid, const std::string& password);
};