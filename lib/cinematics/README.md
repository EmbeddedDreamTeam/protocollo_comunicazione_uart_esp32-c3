# Sottosistema Cinematica — Servo

Ambito: gestione del movimento del servomotore (profilazione S‑curve jerk‑limited, conversione posizione→PWM, compensazioni meccaniche). Responsabilità: Mattia Pistollato (just-normal-username).

## File principali
- lib/cinematics/servo_types.*
- lib/cinematics/servo_motion.*
- lib/cinematics/servo_task.*
- lib/cinematics/servo_hal.*
- lib/cinematics/tests.cpp

## Scopo e responsabilità
- Calcolare e applicare profili di movimento jerk‑limited (S‑curve) per i servomotori.
- Fornire l'API runtime `move_servo_speed(...)` e la task che esegue lo state‑machine.
- Convertire la posizione logica in duty PWM (LEDC) e applicare compensazioni: trim, backlash, deadzone.

## Panoramica HW ↔ SW
- HW: servomotore pilotato via periferica LEDC dell'ESP32 (PWM). Gestione temporale basata su FreeRTOS tasks e tick.
- SW: API pubblica `move_servo_speed` → enqueue comando in `xServoQueue`. La task `move_servo_speed_task_state_machine` legge la coda e applica il profilo S‑curve ogni 20 ms, aggiornando periodicamente la posizione con `set_servo_pos()`.
- Interfaccia superiore: `task_execute_servo` (src/esp32-c3/main.cpp) estrae i parametri da `Msg*` e chiama `move_servo_speed`.
- A fine movimento viene inviato ack verso il master (send_movement_ack) usando `create_msg`/`send_msg_to_master`

## Strutture dati principali
- `ServoTaskParams` { target_rad, speed, acc, jerk } — elementi messi in `xServoQueue`.
- `ServoData` — stato globale: pos/speed/acc sono `std::atomic<float>`; limiti max e parametri HW.
- `MotionPhase` — 7 fasi S‑curve (ACCEL_JUP, ACCEL_CONST, ACCEL_JDN, CRUISE, DECEL_JUP, DECEL_CONST, DECEL_JDN).

## Algoritmo chiave
- Profilo S‑curve jerk‑limited con transizioni di fase controllate dalla distanza residua `rem` e dalla distanza di arresto `d_stop`.
- `d_stop` è calcolata con `decel_distance_sim(...)`, una simulazione numerica a passo `dt` (scelta per ridurre errori dovuti alla quantizzazione temporale).
- La FSM è preemptive: se arriva un nuovo comando in coda durante l'esecuzione, la routine cattura il nuovo comando e ri‑pianifica immediatamente.
- Controllo dei limiti: speed/acc/jerk sono clampati ai massimi configurati in servo_data.
- Ogni iterazione calcola dt usando FreeRTOS ticks: dt = (now - xPrevTick) * (portTICK_PERIOD_MS / 1000.0f)
- Applica protezioni: se rem (distanza residua) <= d_trig (distanza di arresto) si passa alla fase di decelerazione.

## Compensazioni e protezioni
- Backlash compensation: Se il target è minore della posizione corrente il movimento eseguito viene aumentato di un valore di backlash (target = max(target - backlash, min_pos)), si esegue il movimento e, a fine, si effettua un movimento di ritorno (backlash_cmd) per assicurarsi che una certa posizione venga raggiunta sempre nello stesso verso rendendo prevedibile il gioco meccanico.
- Deadzone: Per evitare che il servo scarti comandi molto vicini tra loro, viene applicata una soglia servo_deadzone: se |pos - target| > deadzone si invia pos; altrimenti si imposta direttamente target.
- Trim: Viene aggiunto un offset trim (trim = 0.07 * π) usato nella conversione posizione→duty per calibrare la meccanica.
- Clamp su speed/acc/jerk ai massimi definiti in `servo_data`.

## Testing
- `lib/cinematics/tests.cpp` fornisce test funzionali: sweep, precision, reactivity (preemption), speed/acc/jerk coverage.
- Questi test usano funzioni di logging e controlli su servo_data.current_pos / current_speed / current_acc per rilevare uscita dai limiti e mismatch finale.

## Interfaccia verso altri moduli
- Ingresso: `move_servo_speed(rad, speed, acc, jerk)`
- Messaging: `task_execute_servo` riceve `Msg*` da `h_queue_servo`, estrae parametri e chiama `move_servo_speed`.
- Acknowledgement: a fine movimento viene inviato un `type_servo_ack` al master tramite `send_msg_to_master(create_msg(...))` (ownership del `Msg*` è trasferita al layer di invio).

## Codice rappresentativo (algoritmo avanzato)
Il seguente estratto mostra la funzione `decel_distance_sim(...)`, il cuore numerico utilizzato per stimare la distanza di arresto con limiti di accelerazione e jerk. Questa simulazione guida le transizioni della FSM S‑curve.

```c
float decel_distance_sim(float v,float a,float A,float J,float Vmax){
  if(v<=0) return 0;
  const float dt=0.002f; float x=0;
  for(int i=0;i<20000 && v>1e-6f;++i){
    float targ = -A;
    float j = (a>targ) ? -J : 0.0f;
    float a_next = a + j*dt; if(a_next<targ) a_next=targ;
    float a_avg = 0.5f*(a + a_next);
    float v_next = v + a_avg*dt; if(v_next>Vmax) v_next=Vmax;
    if(v_next<=0.0f){ float t=(a_avg==0)?dt:(-v/a_avg); x+=v*t+0.5f*a_avg*t*t; return x; }
    x += v*dt + 0.5f*a_avg*dt*dt; v=v_next; a=a_next;
  }
  return x;
}
```

## Problemi e risoluzioni osservate:
- Inaccuratezza delle formule analitiche per distanza di arresto → risolto con decel_distance_sim (simulazione numerica con dt piccolo).
- Problemi di ownership Messaggi → centralizzazione allocate_msg / free_msg e commenti per non cancellare msg prima che il task di invio li distrugga.
- Oscillazioni / errori di posizionamento a causa di deadzone / backlash → introdotti trim, deadzone e sequenza di backlash compensation.
- Per evitare grandi picchi di corrente allo start, hai introdotto un delay casuale in servo_init per desincronizzare l’avvio dei servos.

## Note operative e suggerimenti
- 

---