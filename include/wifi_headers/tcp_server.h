#pragma once

#include <string>
#include <functional>

// Callback chiamata quando arriva una riga completa dal Mac
using TcpReceiveCallback = std::function<void(const std::string& line)>;

// Callback chiamata quando il Mac si connette
using TcpConnectCallback = std::function<void()>;

class TcpServer {
public:
    /**
     * Avvia il server TCP in ascolto sulla porta specificata.
     * Accetta una connessione alla volta; quando il Mac si disconnette
     * il server torna automaticamente in ascolto.
     *
     * @param port        Porta TCP (es. 3333)
     * @param on_receive  Callback chiamata ad ogni riga ricevuta dal Mac
     * @param on_connect  Callback chiamata appena il Mac si connette
     */
    static void start(uint16_t port,
                      TcpReceiveCallback on_receive,
                      TcpConnectCallback on_connect = nullptr);

    /**
     * Invia una stringa al Mac connesso (aggiunge automaticamente '\n').
     * Non fa nulla se nessun client è connesso.
     */
    static void send(const std::string& data);

    /** Ritorna true se un client è connesso */
    static bool is_connected();
};