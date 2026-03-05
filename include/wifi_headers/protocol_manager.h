#pragma once

#include <stdint.h>
#include <vector>
#include <functional>
#include <string>

// Callback chiamata quando arriva un comando valido dal computer.
// Contiene gli angoli dei servomotori (1-5 valori, range 0-270).
using ServoCommandCallback = std::function<void(const std::vector<uint16_t>& angles)>;

class ProtocolManager {
public:
    /**
     * Inizializza il protocollo.
     *
     * @param num_servos      Numero di servomotori collegati (1-5).
     *                        Inviato al computer ad ogni cambio.
     * @param on_servo_command Callback chiamata quando arriva un comando valido.
     */
    static void init(uint8_t num_servos, ServoCommandCallback on_servo_command);

    /**
     * Chiama questa funzione ogni volta che il numero di periferiche cambia.
     * Invia automaticamente il nuovo conteggio al computer.
     */
    static void set_num_servos(uint8_t num_servos);

    /**
     * Chiama questa funzione quando arriva una riga grezza dal TCP server.
     * Esegue il parsing, la validazione e invoca la callback o invia un errore.
     */
    static void handle_incoming(const std::string& line);
};