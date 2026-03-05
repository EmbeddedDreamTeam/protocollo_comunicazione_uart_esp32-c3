#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "wifi_manager.h"
#include "tcp_server.h"
#include "protocol_manager.h"

static const char* TAG = "Main";

// -----------------------------------------------------------------------
// Configurazione — modifica qui SSID, password, porta e numero di servos
// -----------------------------------------------------------------------
static constexpr char     AP_SSID[]     = "ESP32-C3-AP";
static constexpr char     AP_PASSWORD[] = "12345678";
static constexpr uint16_t TCP_PORT      = 3333;
static constexpr uint8_t  NUM_SERVOS    = 3;   // ← cambia in base ai servos collegati
// -----------------------------------------------------------------------


void init_wifi(){
    ESP_LOGI(TAG, "Avvio ESP32-C3 Wi-Fi TCP...");

    // 2. Avvia Access Point Wi-Fi
    WifiManager::init_ap(AP_SSID, AP_PASSWORD);

    // 3. Inizializza protocollo — invia subito "SERVOS 3" al computer alla connessione
    ProtocolManager::init(NUM_SERVOS, [](const std::vector<uint16_t>& angles) {
        ESP_LOGI(TAG, "Comando servo ricevuto:");
        for (int i = 0; i < (int)angles.size(); i++) {
            ESP_LOGI(TAG, "  Servo %d → %d°", i, angles[i]);
        }
        // TODO: qui aggiungi il codice per muovere fisicamente i servomotori
    });

    // 4. Avvia TCP server — invia SERVOS appena il computer si connette
    // [SPIEGAZIONE] Funzione Lambda (la parte [ ](const std::string& line) { ... } ).
    // Viene passata come parametro al server TCP. Il server la invocherà ("callback")
    // solamente quando riceve una stringa completa di 'a-capo' dal Mac.
    TcpServer::start(TCP_PORT,
        [](const std::string& line) {               // on_receive
            ProtocolManager::handle_incoming(line);
        },
        []() {                                       // on_connect
            ProtocolManager::set_num_servos(NUM_SERVOS);
        }
    );

    ESP_LOGI(TAG, "Pronto! Connettiti alla rete '%s' e usa:", AP_SSID);
    ESP_LOGI(TAG, "  nc 192.168.4.1 %d", TCP_PORT);

    while (true) {
        // Heartbeat serve per mostrare che la scehda rimane attiva, meglio utilizzarlo solo per debuggare
        // if (TcpServer::is_connected()) {
        //     TcpServer::send("Heartbeat #" + std::to_string(counter++));
        // }
        
        // [SPIEGAZIONE] A differenza di delay() di Arduino che blocca la CPU, 
        // vTaskDelay dice al sistema operativo: "Metti a dormire questo task per 5000ms 
        // ed esegui altri processi nel frattempo".
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}