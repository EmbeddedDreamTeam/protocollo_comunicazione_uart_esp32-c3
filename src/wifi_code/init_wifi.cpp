#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "wifi_manager.h"
#include "tcp_server.h"
#include "protocol_manager.h"
#include "uart_headers/utils_communication.h"

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
    ProtocolManager::init(NUM_SERVOS, [](const std::vector<float>& angles) {
        ESP_LOGI(TAG, "Comando servo ricevuto:");
        for (int i = 0; i < (int)angles.size(); i++) {
            ESP_LOGI(TAG, "  Servo %d → %d°", i, angles[i]);
        }
        // CHIAMATA AL BRIDGE: Questo invia i messaggi via UART/Coda Locale
        convert_servo_instructions(angles);
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

}