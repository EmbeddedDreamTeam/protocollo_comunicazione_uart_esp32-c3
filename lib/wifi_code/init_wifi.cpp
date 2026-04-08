#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "wifi_manager.h"
#include "tcp_server.h"
#include "protocol_manager.h"
#include "utils_uart_comms.h"
#include "protocol_manager.h"

static const char* TAG = "Main";

// -----------------------------------------------------------------------
// Configurazione — modifica qui SSID, password, porta e numero di servos
// -----------------------------------------------------------------------
static constexpr char     AP_SSID[]     = "ESP32-C3-AP";
static constexpr char     AP_PASSWORD[] = "12345678";
static constexpr uint16_t TCP_PORT      = 3333;
// -----------------------------------------------------------------------

void init_wifi(){
    ESP_LOGI(TAG, "Avvio ESP32-C3 Wi-Fi TCP...");

    // 2. Avvia Access Point Wi-Fi
    WifiManager::init_ap(AP_SSID, AP_PASSWORD);

    // 3. Inizializza protocollo — invia "SERVOS 1" al computer alla connessione
    ProtocolManager::init(1, [](const std::vector<float>& angles, 
                                const std::vector<float>& velocities, 
                                const std::vector<float>& accelerations, 
                                const std::vector<float>& jerks) { //! Riceve i 4 vettori
        
        ESP_LOGI(TAG, "Comandi servo ricevuti:");
        for (int i = 0; i < (int)angles.size(); i++) {
            // Uso %.1f perché i valori sono float. Modifica il numero dopo il punto per più o meno decimali.
            ESP_LOGI(TAG, "  Servo %d → Angolo: %.1f°, Vel: %.1f, Acc: %.1f, Jerk: %.1f", 
                     i, angles[i], velocities[i], accelerations[i], jerks[i]);
        }

        // CHIAMATA AL BRIDGE: Questo invia i messaggi via UART/Coda Locale
        // ATTENZIONE: Ricordati di aggiornare anche la funzione convert_servo_instructions
        // affinché accetti i nuovi parametri!
        convert_servo_instructions(angles, velocities, accelerations, jerks);
    });

    // 4. Avvia TCP server — invia SERVOS appena il computer si connette
    // [SPIEGAZIONE] Funzione Lambda (la parte [ ](const std::string& line) { ... } ).
    // Viene passata come parametro al server TCP. Il server la invocherà ("callback")
    // solamente quando riceve una stringa completa di 'a-capo' dal Mac.
    TcpServer::start(TCP_PORT,
        [](const std::string& line) {               // on_receive
            ProtocolManager::handle_incoming(line);
        },
        []() {
            reply("SERVOS " + std::to_string(s_num_servos));                                       // on_connect
        }
    );

    ESP_LOGI(TAG, "Pronto! Connettiti alla rete '%s' e usa:", AP_SSID);
    ESP_LOGI(TAG, "  nc 192.168.4.1 %d", TCP_PORT);

}