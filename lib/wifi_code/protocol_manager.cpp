#include "protocol_manager.h"
#include "tcp_server.h"

#include <sstream>
#include <string>
#include "esp_log.h"

static const char* TAG = "ProtocolManager";

static constexpr uint8_t  MAX_SERVOS   = 5;
static constexpr float ANGLE_MIN    = -139.0;
static constexpr float ANGLE_MAX    = +139.0;

namespace {
    static uint8_t               s_num_servos = 1;
    static ServoCommandCallback  s_on_command;
}

// ---------------------------------------------------------------------------
// Internal: send a message to the computer via TCP
// ---------------------------------------------------------------------------
static void reply(const std::string& msg)
{
    TcpServer::send(msg);
    ESP_LOGI(TAG, "-> Computer: %s", msg.c_str());
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ProtocolManager::init(uint8_t num_servos, ServoCommandCallback on_servo_command)
{
    s_on_command = on_servo_command;
    s_num_servos = num_servos;
    // Non inviamo nulla qui — il computer non è ancora connesso.
    // set_num_servos() verrà chiamato dalla on_connect del TcpServer.
}

void ProtocolManager::set_num_servos(uint8_t num_servos) //!HERE
{
    s_num_servos = num_servos;

    // Notify the computer immediately
    reply("SERVOS " + std::to_string(s_num_servos));
    ESP_LOGI(TAG, "Periferiche collegate: %d", s_num_servos);
}

#include <cstdlib> 

void ProtocolManager::handle_incoming(const std::string& line)
{
    ESP_LOGI(TAG, "<- Computer: %s", line.c_str());

    std::vector<float> angles;
    std::istringstream stream(line);
    std::string token;

    while (stream >> token) {
        char* endptr = nullptr;
        
        // strtof prende una stringa C-style (const char*) e un puntatore a char (endptr).
        float value = std::strtof(token.c_str(), &endptr);
        
        // Controllo errori senza eccezioni:
        // 1. Se endptr è uguale all'inizio della stringa, non è stato trovato nessun numero (es. "ciao").
        // 2. Se *endptr non è il terminatore nullo '\0', ci sono caratteri spuri alla fine (es. "139.9abc").
        if (endptr == token.c_str() || *endptr != '\0') {
            reply("ERROR invalid_format — expected numbers (e.g. -139.9) separated by spaces");
            return;
        }

        // Controllo del range
        if (value < ANGLE_MIN || value > ANGLE_MAX) {
            std::string error_msg = "ERROR angle_out_of_range — value " + token +
                                    " is outside allowed range [" + 
                                    std::to_string(ANGLE_MIN) + ", " + 
                                    std::to_string(ANGLE_MAX) + "]";
            reply(error_msg);
            return;
        }

        angles.push_back(value);

        if (angles.size() > MAX_SERVOS) {
            reply("ERROR too_many_values — max allowed is " + std::to_string(MAX_SERVOS));
            return;
        }
    }

    if (angles.empty()) {
        reply("ERROR empty_command — send angles separated by spaces, e.g. '0 90.5 -139.9'");
        return;
    }

    // Validate count matches connected servos
    if (angles.size() != s_num_servos) {
        reply("ERROR wrong_count — received " + std::to_string(angles.size()) +
              " values but " + std::to_string(s_num_servos) + " servos are connected");
        return;
    }

    // All good — invoke callback
    reply("OK");
    if (s_on_command) s_on_command(angles);
}