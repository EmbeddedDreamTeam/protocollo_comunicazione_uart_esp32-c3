#include "protocol_manager.h"
#include "tcp_server.h"

#include <sstream>
#include <string>
#include "esp_log.h"

static const char* TAG = "ProtocolManager";

static constexpr uint8_t  MAX_SERVOS   = 5;
static constexpr uint16_t ANGLE_MIN    = 0;
static constexpr uint16_t ANGLE_MAX    = 270;

namespace {
    static uint8_t               s_num_servos = 0;
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

void ProtocolManager::set_num_servos(uint8_t num_servos)
{
    s_num_servos = num_servos;

    // Notify the computer immediately
    reply("SERVOS " + std::to_string(s_num_servos));
    ESP_LOGI(TAG, "Periferiche collegate: %d", s_num_servos);
}

void ProtocolManager::handle_incoming(const std::string& line)
{
    ESP_LOGI(TAG, "<- Computer: %s", line.c_str());

    // Parse space-separated integers
    std::vector<uint16_t> angles;
    std::istringstream stream(line);
    std::string token;

    while (stream >> token) {
        // Check each token is a valid integer
        for (char c : token) {
            if (!isdigit(c)) {
                reply("ERROR invalid_format — expected integers separated by spaces");
                return;
            }
        }

        int value = std::stoi(token);

        if (value < ANGLE_MIN || value > ANGLE_MAX) {
            reply("ERROR angle_out_of_range — value " + std::to_string(value) +
                  " is outside allowed range [0, 270]");
            return;
        }

        angles.push_back(static_cast<uint16_t>(value));

        if (angles.size() > MAX_SERVOS) {
            reply("ERROR too_many_values — max allowed is " +
                  std::to_string(MAX_SERVOS));
            return;
        }
    }

    if (angles.empty()) {
        reply("ERROR empty_command — send angles separated by spaces, e.g. '0 90 180'");
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