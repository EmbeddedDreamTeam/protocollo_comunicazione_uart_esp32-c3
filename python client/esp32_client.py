"""
ESP32-C3 Servo Controller — Python Client (Auto/Manual Flag Edition)
------------------------------------------
- Connects to the ESP32 Access Point via TCP
- Uses a flag to choose between Automatic Sequence or Manual Input
- Waits for an 'ACK' from ESP32 before sending the next step in Auto mode
"""

import socket
import threading
import sys
import time

# ===========================================================================
# CONFIGURAZIONE PRINCIPALE
# ===========================================================================
ESP_HOST = "192.168.4.1"
ESP_PORT = 3333

# FLAG DI MODALITÀ:
# True  -> Esegue automaticamente la sequenza e poi si ferma.
# False -> Entra in modalità manuale e aspetta il tuo input da tastiera.
AUTO_MODE = False
# ===========================================================================

num_servos = None

# Sincronizzazione thread
ack_event = threading.Event()   # Semaforo per l'attesa dell'ACK tra un movimento e l'altro
ready_event = threading.Event() # Semaforo per aspettare che l'ESP32 sia connesso e pronto

# La sequenza di test
TARGET_SEQUENCE = [
    [0, 0, 0, 0],

    [-139, -139, -139, -139],
    [-139, -139, -139, -139],

    
    # Target: [-139, -60, -60, -139] (4 motori da muovere -> diviso in 2 step)
    [-139, -60, 0, 0],
    [-139, -60, -60, -139],
    
    # Target: [139, -60, -60, 139] (2 motori da muovere -> 1 step)
    [139, -60, -60, 139],
    
    # Target: [139, -90, 139, 0] (3 motori da muovere -> diviso in 2 step)
    [139, -90, 139, 139],
    [139, -90, 139, 0],
    
    # Target: [-139, -90, 100, 0] (2 motori da muovere -> 1 step)
    [-139, -90, 100, 0],
    
    # Target: [-139, 0, -120, 0] (2 motori da muovere -> 1 step)
    [-139, 0, -120, 0],
    
    # Target: [0, 0, 120, 139] (3 motori da muovere -> diviso in 2 step)
    [0, 0, 120, 0],
    [0, 0, 120, 139],
    
    # Target: [0, 60, 60, -139] (3 motori da muovere -> diviso in 2 step)
    [0, 60, 60, 139],
    [0, 60, 60, -139],
    
    # Target: [139, 60, 139, 139] (3 motori da muovere -> diviso in 2 step)
    [139, 60, 139, -139],
    [139, 60, 139, 139],
    
    # Target: [-139, 60, -139, -139] (3 motori da muovere -> diviso in 2 step)
    [-139, 60, -139, 139],
    [-139, 60, -139, -139],
    
    # Target: [139, 60, 139, 139] (3 motori da muovere -> diviso in 2 step)
    [139, 60, 139, -139],
    [139, 60, 139, 139],
    
    # Target: [139, 120, 0, 0] (3 motori da muovere -> diviso in 2 step)
    [139, 120, 0, 139],
    [139, 120, 0, 0],
    
    # Target: [-139, 120, 0, 0] (1 motore da muovere -> 1 step)
    [-139, 120, 0, 0],
    
    # Target: [-139, 120, 120, 0] (1 motore da muovere -> 1 step)
    [-139, 120, 120, 0],
    
    # Target: [-139, 0, 120, 120] (2 motori da muovere -> 1 step)
    [-139, 0, 120, 120],
    
    # Target: [-139, 60, -60, -139] (3 motori da muovere -> diviso in 2 step)
    [-139, 60, -60, 120],
    [-139, 60, -60, -139],
    
    # Target: [139, 60, -60, -139] (1 motore da muovere -> 1 step)
    [139, 60, -60, -139],
    
    # Target: [0, 0, 0, 0] (4 motori da muovere -> diviso in 2 step)
    [0, 0, -60, -139],

    [0, 0, 0, 0]
]
   

# ---------------------------------------------------------------------------
# Receiver thread
# ---------------------------------------------------------------------------
def receiver(sock: socket.socket) -> None:
    global num_servos
    buffer = ""

    while True:
        try:
            chunk = sock.recv(1024).decode("utf-8")
            if not chunk:
                print("\n[disconnected] L'ESP32 ha chiuso la connessione.")
                # Sblocchiamo eventuali eventi appesi prima di uscire
                ready_event.set() 
                ack_event.set()
                sys.exit(0)

            buffer += chunk
            while "\n" in buffer:
                line, buffer = buffer.split("\n", 1)
                line = line.strip()
                if not line:
                    continue

                # Cerca l'ACK
                if "OK" or "ACK" in line.upper() or "DONE" in line.upper():
                    ack_event.set()

                # Parse SERVOS message all'avvio
                if line.startswith("SERVOS "):
                    try:
                        num_servos = int(line.split()[1])
                        print(f"\n[esp32] Rilevati {num_servos} servi connessi.")
                        ready_event.set() # Diamo il via libera al main()
                    except (IndexError, ValueError):
                        print(f"\n[esp32] {line}")
                else:
                    print(f"\n[esp32] {line}")

        except OSError:
            print("\n[disconnected] Connessione persa.")
            sys.exit(0)


# ---------------------------------------------------------------------------
# Funzione Sequenza Automatica
# ---------------------------------------------------------------------------
def run_sequence(sock: socket.socket) -> None:
    print("\n[sequence] Avvio sequenza automatica tra 15 secondi...")
    time.sleep(2.0) # Piccola pausa di sicurezza per far stabilizzare l'hardware
    
    for step_idx, angles in enumerate(TARGET_SEQUENCE):
        message = " ".join(str(a ) for a in angles) + "\n"
        
        ack_event.clear()
        
        print(f"[sequence] Invio step {step_idx + 1}/{len(TARGET_SEQUENCE)}: {message.strip()}")
        sock.sendall(message.encode("utf-8"))
        
        # Aspetta l'ACK
        ack_received = ack_event.wait(timeout=15.0)
        
        if not ack_received:
            print(f"[error] Timeout! Nessun ACK ricevuto per lo step {step_idx + 1}. Interrompo.")
            break
            
        print("[sequence] ACK ricevuto! Procedo...\n")
        time.sleep(3) # Pausa tra i movimenti

    print("[sequence] Test automatico completato!")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main() -> None:
    print(f"Connessione all'ESP32 su {ESP_HOST}:{ESP_PORT} ...")

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((ESP_HOST, ESP_PORT))
    except Exception as e:
        print(f"[error] Impossibile connettersi: {e}")
        sys.exit(1)

    print("Connesso. In attesa dei dati di avvio dall'ESP32...\n")

    t = threading.Thread(target=receiver, args=(sock,), daemon=True)
    t.start()

    # Aspettiamo che l'ESP32 comunichi di essere pronto (timeout 5 sec)
    is_ready = ready_event.wait(timeout=5.0)
    
    if not is_ready:
        print("[warning] Non ho ricevuto il setup iniziale dall'ESP32, ma provo a continuare lo stesso.")

    # ---------------------------------------------------------
    # DIRAMAZIONE LOGICA BASATA SULLA FLAG
    # ---------------------------------------------------------
    time.sleep(15)
    if AUTO_MODE:
        # Modalità Automatica
        run_sequence(sock)
        print("Chiudo la connessione.")
        sock.close()
        sys.exit(0)
        
    else:
        # Modalità Manuale
        print("\n=======================================================")
        print(" MODALITÀ MANUALE ATTIVA")
        print(" Inserisci gli angoli separati da spazio e premi Invio.")
        print(" (es: 90.5 0 -45.2 120)")
        print(" Scrivi 'quit' per uscire.")
        print("=======================================================\n")
        
        try:
            while True:
                raw = input("> ").strip()

                if raw.lower() in ("quit", "exit", "q"):
                    print("Chiudo la connessione.")
                    sock.close()
                    sys.exit(0)

                if not raw:
                    continue

                message = raw + "\n"
                sock.sendall(message.encode("utf-8"))

        except (KeyboardInterrupt, EOFError):
            print("\nChiudo la connessione.")
            sock.close()
            sys.exit(0)

if __name__ == "__main__":
    main()