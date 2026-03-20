"""
ESP32-C3 Servo Controller — Python Client
------------------------------------------
- Connects to the ESP32 Access Point via TCP
- Reads the number of connected servos automatically
- Lets you send angle commands interactively
- Prints all messages received from the ESP32

Usage:
    python3 esp32_client.py

Requirements:
    Python 3.6+, no external libraries needed.
"""

import socket
import threading
import sys

ESP_HOST = "192.168.4.1"
ESP_PORT = 3333

ANGLE_MIN = 0
ANGLE_MAX = 270

num_servos = None  # will be set when ESP32 sends "SERVOS N"


# ---------------------------------------------------------------------------
# Receiver thread — prints every message coming from the ESP32
# ---------------------------------------------------------------------------
def receiver(sock: socket.socket) -> None:
    global num_servos
    buffer = ""

    while True:
        try:
            chunk = sock.recv(1024).decode("utf-8")
            if not chunk:
                print("\n[disconnected] ESP32 closed the connection.")
                sys.exit(0)

            buffer += chunk
            while "\n" in buffer:
                line, buffer = buffer.split("\n", 1)
                line = line.strip()
                if not line:
                    continue

                # Parse SERVOS message
                if line.startswith("SERVOS "):
                    try:
                        num_servos = int(line.split()[1])
                        print(f"\n[esp32] Connected servos: {num_servos}")
                        print(f"[esp32] Send {num_servos} angle(s) between "
                              f"{ANGLE_MIN} and {ANGLE_MAX}, separated by spaces.")
                        print("        Type 'quit' to exit.\n> ", end="", flush=True)
                    except (IndexError, ValueError):
                        print(f"\n[esp32] {line}")
                else:
                    print(f"\n[esp32] {line}\n> ", end="", flush=True)

        except OSError:
            print("\n[disconnected] Connection lost.")
            sys.exit(0)


# ---------------------------------------------------------------------------
# Input validation
# ---------------------------------------------------------------------------
def validate_command(raw: str) -> list[int] | None:
    """
    Validates the user input and returns a list of angles,
    or None if the input is invalid (prints an error message).
    """
    if num_servos is None:
        print("[wait] Still waiting for servo count from ESP32...")
        return None

    tokens = raw.strip().split()

    if len(tokens) != num_servos:
        print(f"[error] Expected {num_servos} value(s), got {len(tokens)}. "
              f"Example: {' '.join(['90'] * num_servos)}")
        return None

    angles = []
    for token in tokens:
        if not token.isdigit():
            print(f"[error] '{token}' is not a valid integer.")
            return None
        angle = int(token)
        if not (ANGLE_MIN <= angle <= ANGLE_MAX):
            print(f"[error] Angle {angle} is out of range "
                  f"[{ANGLE_MIN}, {ANGLE_MAX}].")
            return None
        angles.append(angle)

    return angles


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main() -> None:
    print(f"Connecting to ESP32 at {ESP_HOST}:{ESP_PORT} ...")

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((ESP_HOST, ESP_PORT))
    except ConnectionRefusedError:
        print("[error] Connection refused. Is the ESP32 running and reachable?")
        sys.exit(1)
    except OSError as e:
        print(f"[error] Could not connect: {e}")
        print("        Make sure your computer is connected to the 'ESP32-C3-AP' Wi-Fi network.")
        sys.exit(1)

    print("Connected.\n")

    # Start background thread to receive messages
    t = threading.Thread(target=receiver, args=(sock,), daemon=True)
    t.start()

    # Main thread handles user input
    try:
        while True:
            raw = input("> ").strip()

            if raw.lower() in ("quit", "exit", "q"):
                print("Closing connection.")
                sock.close()
                sys.exit(0)

            if not raw:
                continue

            angles = validate_command(raw)
            if angles is None:
                continue

            message = " ".join(str(a) for a in angles) + "\n"
            sock.sendall(message.encode("utf-8"))

    except (KeyboardInterrupt, EOFError):
        print("\nClosing connection.")
        sock.close()
        sys.exit(0)


if __name__ == "__main__":
    main()
