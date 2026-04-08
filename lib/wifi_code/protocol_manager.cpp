#include "protocol_manager.h"
#include "tcp_server.h"

#include <sstream>
#include <string>
#include "esp_log.h"

static const char* TAG = "ProtocolManager";

static constexpr uint8_t  MAX_SERVOS   = 5;
static constexpr float ANGLE_MIN    = -139.0;
static constexpr float ANGLE_MAX    = +139.0;
static constexpr float MIN_SPEED    =  0.1;
static constexpr float MAX_SPEED = 5.2;
static constexpr float MIN_ACC = 0.1;
static constexpr float MAX_ACC = 100.0;
static constexpr float MIN_JERK = 000.1;
static constexpr float MAX_JERK = 1500.0;


uint8_t s_num_servos = 1;

namespace {
    static ServoCommandCallback  s_on_command;
}

// ---------------------------------------------------------------------------
// Internal: send a message to the computer via TCP
// ---------------------------------------------------------------------------
void reply(const std::string& msg)
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
    std::vector<float> velocities;
    std::vector<float> accelerations;
    std::vector<float> jerks;

    std::istringstream stream(line);
    std::string token;
    
    int token_count = 0; // Contatore dei valori letti

    while (stream >> token) {
        char* endptr = nullptr;
        float value = std::strtof(token.c_str(), &endptr);
        
        // Controllo validità formato numero (exception-free)
        if (endptr == token.c_str() || *endptr != '\0') {
            reply("ERROR invalid_format — expected numbers separated by spaces");
            return;
        }

        // Determiniamo quale parametro stiamo leggendo (0=angolo, 1=vel, 2=acc, 3=jerk)
        int param_type = token_count % 4;

        if (param_type == 0) { // ANGOLO
            if (value < ANGLE_MIN || value > ANGLE_MAX) {
                reply("ERROR angle_out_of_range — value " + token + 
                      " not in [" + std::to_string(ANGLE_MIN) + ", " + std::to_string(ANGLE_MAX) + "]");
                return;
            }
            angles.push_back(value);
            
            // Check sul numero massimo di servi (fatto solo quando leggiamo un nuovo angolo)
            if (angles.size() > MAX_SERVOS) {
                reply("ERROR too_many_servos — max allowed is " + std::to_string(MAX_SERVOS));
                return;
            }
            
        } else if (param_type == 1) { // VELOCITÀ
            if (value < MIN_SPEED || value > MAX_SPEED) {
                reply("ERROR speed_out_of_range — value " + token + 
                      " not in [" + std::to_string(MIN_SPEED) + ", " + std::to_string(MAX_SPEED) + "]");
                return;
            }
            velocities.push_back(value);
            
        } else if (param_type == 2) { // ACCELERAZIONE
            if (value < MIN_ACC || value > MAX_ACC) {
                reply("ERROR acc_out_of_range — value " + token + 
                      " not in [" + std::to_string(MIN_ACC) + ", " + std::to_string(MAX_ACC) + "]");
                return;
            }
            accelerations.push_back(value);
            
        } else if (param_type == 3) { // JERK
            if (value < MIN_JERK || value > MAX_JERK) {
                reply("ERROR jerk_out_of_range — value " + token + 
                      " not in [" + std::to_string(MIN_JERK) + ", " + std::to_string(MAX_JERK) + "]");
                return;
            }
            jerks.push_back(value);
        }

        token_count++;
    }

    // 1. Controllo che la stringa non fosse vuota
    if (token_count == 0) {
        reply("ERROR empty_command — send values as: angle vel acc jerk ...");
        return;
    }

    // 2. Controllo che i valori siano arrivati in pacchetti completi da 4
    if (token_count % 4 != 0) {
        reply("ERROR incomplete_data — each servo requires exactly 4 parameters (angle, speed, acc, jerk)");
        return;
    }

    // 3. Controllo che il numero di "gruppi" corrisponda al numero di servi connessi
    if (angles.size() != s_num_servos) {
        reply("ERROR wrong_count — expected data for " + std::to_string(s_num_servos) + 
              " servos, but received data for " + std::to_string(angles.size()));
        return;
    }

    // Tutto OK — invochiamo la callback
    reply("OK");
    
    // ATTENZIONE: Ora devi passare tutti e 4 i vettori alla callback!
    if (s_on_command) {
        s_on_command(angles, velocities, accelerations, jerks);
    }
}