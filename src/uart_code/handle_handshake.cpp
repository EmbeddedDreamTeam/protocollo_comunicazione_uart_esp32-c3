#include <stdio.h>
#include <string.h>

//definiti da me:
#include "msg_structs.h"
#include "utils_communication.h"


//* _______________________________________ GESTIONE DI HANDSHAKE



//todo STO RISCRIVENDO COMPLETAMENTE QUESTO FILE QUA SOTTO, ANCHE HANDLER REPORT é DA RIVEDERE


void send_report_to_root(){
  Payload p;
  p.payload_report.my_id = SELF_ID;
  p.payload_report.my_master_id = MASTER_ID;
  p.payload_report.my_slave_id = SLAVE_ID;
  Msg* m = create_msg(SELF_ID, ROOT_ID, type_report, p);
  send_msg_to_master(m);
}


volatile int last_MtS_ack_sender_id = -1;
volatile bool received_MtS_ack = false;

volatile int last_StM_ack_sender_id = -1;
volatile bool received_StM_ack = false;

void task_ping_slave(void* info){ // mando MtS a slave
  while(1){
    Payload p;
    p.payload_handshake.handshake_type = type_MtS;
    Msg* msg = create_msg(SELF_ID, UNKNOWN_ID, type_handshake, p); 

    received_MtS_ack = false; 
    send_msg_to_slave(msg);

    vTaskDelay(pdMS_TO_TICKS(1000));
    
    if(received_MtS_ack){ // slave esiste
      if(last_MtS_ack_sender_id != SLAVE_ID){ // è diverso da slave ID
        printf(">>> SLAVE CHANGED FROM %d TO %d\n", SLAVE_ID, last_MtS_ack_sender_id);

        SLAVE_ID = last_MtS_ack_sender_id; 
        send_buffered_messages_to_slave();
        send_report_to_root();
      }
    } else { // slave non esiste
      printf(">>> SLAVE %d DOESNT RESPOND, I ASSUME HE ISNT THERE (SLAVE_ID = -1)\n", SLAVE_ID);
      SLAVE_ID = UNKNOWN_ID; 
      send_report_to_root();
    }
  }
}


void task_ping_master(void* info){ 
  while(1){
    Payload p;
    p.payload_handshake.handshake_type = type_StM;
    Msg* msg = create_msg(SELF_ID, UNKNOWN_ID, type_handshake, p); 

    received_StM_ack = false; 
    send_msg_to_master(msg);

    vTaskDelay(pdMS_TO_TICKS(1000));
    
    if(received_StM_ack){ // master esiste
      if(last_StM_ack_sender_id != MASTER_ID){ // è diverso da master ID
        printf(">>> MASTER CHANGED FROM %d TO %d\n", MASTER_ID, last_StM_ack_sender_id);

        MASTER_ID = last_StM_ack_sender_id; 
        send_buffered_messages_to_master();
        send_report_to_root();
      }
    } else { // master non esiste
      printf(">>> MASTER %d DOESNT RESPOND, I ASSUME HE ISNT THERE (MASTER_ID = -1)\n", MASTER_ID);
      MASTER_ID = UNKNOWN_ID; 
      // todo NON MANDARE IL REPORT NON PUO GESTIRLO
    }
  }
}


void task_handle_handshakes(void* info){
  while(1){
    Msg *msg = nullptr;
    xQueueReceive(h_queue_handshake, &msg, portMAX_DELAY);

    if(msg->payload.payload_handshake.handshake_type == type_MtS){ // il master fa ciao rispondigli
      if(msg->sender_id != MASTER_ID){
        printf(">>> MASTER CHANGED FROM %d TO %d\n", MASTER_ID, msg->sender_id);
        MASTER_ID = msg->sender_id; 
        send_report_to_root(); //so che non è -1 in quanto ho ricevuto un messaggio da qualcuno;
      }

      Payload p;
      p.payload_handshake.handshake_type = type_MtS_ack;
      Msg* nm = create_msg(SELF_ID, UNKNOWN_ID, type_handshake, p);
      send_msg_to_master(nm);

    } else if(msg->payload.payload_handshake.handshake_type == type_MtS_ack){
      last_MtS_ack_sender_id = msg->sender_id;
      received_MtS_ack = true; // FIX: consistency (true instead of 1)

    } else if(msg->payload.payload_handshake.handshake_type == type_StM){
      if(msg->sender_id != SLAVE_ID){
        printf(">>> SLAVE CHANGED FROM %d TO %d\n", SLAVE_ID, msg->sender_id);
        SLAVE_ID = msg->sender_id; 
        send_report_to_root();
      }

      Payload p;
      p.payload_handshake.handshake_type = type_StM_ack;
      Msg* nm = create_msg(SELF_ID, UNKNOWN_ID, type_handshake, p);
      send_msg_to_slave(nm);

    } else if(msg->payload.payload_handshake.handshake_type == type_StM_ack){
      last_StM_ack_sender_id = msg->sender_id;
      received_StM_ack = true; 
    }

    free(msg);  
  }
}

/*
X invia type_MtS / type_StM ciclicamente:
Y capisce chi è X, potrebbe essere cambiato, in caso aggiorna MY_MASTER e MY_SLAVE e manda un report a ROOT;
Y iniva type_MtS_ack / type_StM_ack a X (SOLO IN RISPOSTA AD UN type_MtS / type_StM);
X capisce chi è X, potrebbe essere cambiato, in caso aggiorna MY_MASTER e MY_SLAVE e manda un report a ROOT;
*/