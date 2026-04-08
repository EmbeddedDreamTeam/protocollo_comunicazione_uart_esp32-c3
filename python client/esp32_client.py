"""
ESP32-C3 Servo Controller — Python Client (Smart Optional Parameters)
------------------------------------------
- Supporta parametri opzionali: Velocità, Accelerazione e Jerk
- Riconosce automaticamente se si inseriscono parametri globali o specifici
"""

import socket
import threading
import sys
import time

# ===========================================================================
# CONFIGURAZIONE PRINCIPALE E VALORI DI DEFAULT
# ===========================================================================
ESP_HOST = "192.168.4.1"
ESP_PORT = 3333

# FLAG DI MODALITÀ:
# True  -> Esegue automaticamente la sequenza e poi si ferma.
# False -> Entra in modalità manuale e aspetta il tuo input da tastiera.
AUTO_MODE = False

# Valori di default per ogni movimento
DEFAULT_SPEED = 1.0
DEFAULT_ACC = 100.0
DEFAULT_JERK = 1500.0
# ===========================================================================

num_servos = None

# Sincronizzazione thread
ack_event = threading.Event()   
ready_event = threading.Event() 

# La sequenza di test
# ORA PUOI SCRIVERE:
# [0, 0, 0, 0] -> Usa i default
# [90, 90, 90, 90, 2.5] -> Applica velocità 2.5 a tutti per questo step
# [45, 45, 45, 45, 5.0, 50, 1000] -> Applica vel 5.0, acc 50, jerk 1000 a tutti
TARGET_SEQUENCE = [
    [0.0, 0, 0, 0],

    [-139, -139, -139, -139],
    [-139, -139, -139, -139],

    [-139, -60, 0, 0],
    [-139, -60, -60, -139],
    
    [139, -60, -60, 139],
    
    [139, -90, 139, 139],
    [139, -90, 139, 0],
    
    [-139, -90, 100, 0],
    
    [-139, 0, -120, 0],
    
    [0, 0, 120, 0],
    [0, 0, 120, 139],
    
    [0, 60, 60, 139],
    [0, 60, 60, -139],
    
    [139, 60, 139, -139],
    [139, 60, 139, 139],
    
    [-139, 60, -139, 139],
    [-139, 60, -139, -139],
    
    [139, 60, 139, -139],
    [139, 60, 139, 139],
    
    [139, 120, 0, 139],
    [139, 120, 0, 0],
    
    [-139, 120, 0, 0],
    
    [-139, 120, 120, 0],
    
    [-139, 0, 120, 120],
    
    [-139, 60, -60, 120],
    [-139, 60, -60, -139],
    
    [139, 60, -60, -139],
    
    [0, 0, -60, -139],

    [0, 0, 0, 0]
]
   

# ---------------------------------------------------------------------------
# Funzione intelligente di composizione del Payload
# ---------------------------------------------------------------------------
def build_payload(values: list[float], num_servos: int) -> str | None:
    """
    Costruisce la stringa finale per l'ESP32 interpretando i parametri opzionali.
    """
    n = len(values)
    
    # CASO 1: L'utente ha inserito esattamente tutti i parametri per ogni servo (es: 16 valori per 4 servi)
    if n == num_servos * 4:
        return " ".join(str(v) for v in values)
        
    # CASO 2: L'utente ha inserito gli angoli + parametri globali opzionali
    if num_servos <= n <= num_servos + 3:
        angles = values[:num_servos]
        
        # Estrai i parametri globali se forniti, altrimenti usa i default
        speed = values[num_servos] if n > num_servos else DEFAULT_SPEED
        acc = values[num_servos + 1] if n > num_servos + 1 else DEFAULT_ACC
        jerk = values[num_servos + 2] if n > num_servos + 2 else DEFAULT_JERK
        
        parts = []
        for a in angles:
            parts.append(f"{a} {speed} {acc} {jerk}")
            
        return " ".join(parts)
        
    # Errore: quantitativo di numeri non riconosciuto
    return None

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
                ready_event.set() 
                ack_event.set()
                sys.exit(0)

            buffer += chunk
            while "\n" in buffer:
                line, buffer = buffer.split("\n", 1)
                line = line.strip()
                if not line:
                    continue

                line_up = line.upper()
                if "OK" in line_up or "ACK" in line_up or "DONE" in line_up:
                    ack_event.set()

                if line.startswith("SERVOS "):
                    try:
                        num_servos = int(line.split()[1])
                        print(f"\n[esp32] Rilevati {num_servos} servi connessi.")
                        ready_event.set()
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
    print("\n[sequence] Avvio sequenza automatica tra 5 secondi...")
    time.sleep(5.0) 
    
    for step_idx, step_values in enumerate(TARGET_SEQUENCE):
        
        # Usa la nuova logica intelligente
        payload = build_payload(step_values, num_servos)
        
        if payload is None:
            print(f"[error] Step {step_idx + 1} ha un numero non valido di elementi ({len(step_values)}). Salto.")
            continue
            
        message = payload + "\n"
        ack_event.clear()
        
        print(f"[sequence] Invio step {step_idx + 1}/{len(TARGET_SEQUENCE)}: {message.strip()}")
        sock.sendall(message.encode("utf-8"))
        
        ack_received = ack_event.wait(timeout=15.0)
        
        if not ack_received:
            print(f"[error] Timeout! Nessun ACK ricevuto per lo step {step_idx + 1}. Interrompo.")
            break
            
        print("[sequence] ACK ricevuto! Procedo...\n")
        time.sleep(3)

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

    is_ready = ready_event.wait(timeout=5.0)
    
    if not is_ready:
        print("[warning] Non ho ricevuto il setup iniziale dall'ESP32, ma provo a continuare lo stesso.")
        # Se fallisce la lettura iniziale, forziamo num_servos per non bloccare tutto
        global num_servos
        if num_servos is None:
            num_servos = 4 

    # ---------------------------------------------------------
    # DIRAMAZIONE LOGICA BASATA SULLA FLAG
    # ---------------------------------------------------------
    if AUTO_MODE:
        run_sequence(sock)
        print("Chiudo la connessione.")
        sock.close()
        sys.exit(0)
        
    else:
        print("\n=======================================================")
        print(" MODALITÀ MANUALE ATTIVA")
        print(f" Inserisci {num_servos} angoli separati da spazio.")
        print(" Opzionale: aggiungi Vel, Acc e Jerk alla fine per sovrascrivere i default.")
        print(f" Esempi per {num_servos} servi:")
        print("   Solo angoli:   90 0 45 120")
        print("   Con Vel:       90 0 45 120 3.5")
        print("   Con Vel+Acc:   90 0 45 120 3.5 200")
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

                try:
                    # Converte l'input testuale in una lista di float
                    user_values = [float(x) for x in raw.split()]
                except ValueError:
                    print("[error] Formato non valido. Inserisci solo numeri separati da spazio.")
                    continue

                # Usa la funzione intelligente per validare e formattare
                payload = build_payload(user_values, num_servos)
                
                if payload is None:
                    print(f"[error] Hai inserito {len(user_values)} valori. Inseriscine {num_servos} (solo angoli), "
                          f"{num_servos+1}, {num_servos+2}, {num_servos+3} (parametri globali) "
                          f"oppure {num_servos*4} (dati completi).")
                    continue
                
                message = payload + "\n"
                sock.sendall(message.encode("utf-8"))

        except (KeyboardInterrupt, EOFError):
            print("\nChiudo la connessione.")
            sock.close()
            sys.exit(0)

if __name__ == "__main__":
    main()