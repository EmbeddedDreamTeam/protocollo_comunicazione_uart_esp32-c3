#include <stdio.h>
#include <string.h>

//definiti da me:
#include "msg_structs.h"
#include "utils_communication.h"


//* _______________________________________ GESTIONE DI HANDSHAKE
bool hello_msg_from_slave_recived = false;
bool hello_msg_from_master_recived = false;
bool report_msg_to_root_sent = false;

void send_handshake_msg_to_slave(){

  Payload p;
  p.payload_handshake.handshake_type = type_ACK_hello;
  Msg* hello_msg = create_msg(SELF_ID, SLAVE_ID, type_handshake, p); 

  send_msg_to_slave(hello_msg);
}


void send_handshake_type_report_to_root(){
  if(SELF_ID == ROOT_ID){
    vTaskDelete(nullptr);
  }
  
  for(int i=0; i<1; i++){ //!!QUI GEMINI!!
    Payload p;
    p.payload_report.my_id = SELF_ID;
    p.payload_report.my_master_id = MASTER_ID;
    p.payload_report.my_slave_id = SLAVE_ID;
    Msg* report_msg = create_msg(SELF_ID, ROOT_ID, type_report, p);

    // xQueueSend(h_queue_send_to_master, &report_msg, portMAX_DELAY);
    send_msg_to_master(report_msg);
  }
}


void task_handle_handshake(void *arg){ 
  while(1){
    Msg *msg = nullptr;
    xQueueReceive(h_queue_handshake, &msg, portMAX_DELAY);

    if(msg->type == type_handshake && msg->payload.payload_handshake.handshake_type == type_hello){ //from SLAVE
      SLAVE_ID = msg->sender_id;
      hello_msg_from_slave_recived = true;
      send_handshake_msg_to_slave();
      if(SELF_ID == ROOT_ID){
        init_report_handler(SLAVE_ID); //necessario sapere chi è lo slave della root immediatamente
      }
      send_buffered_messages_to_slave();

      delete msg; 

    } else if(msg->type == type_handshake && msg->payload.payload_handshake.handshake_type == type_ACK_hello){ //from MASTER
      MASTER_ID = msg->sender_id;
      hello_msg_from_master_recived = true;
      send_buffered_messages_to_master();
      
      delete msg; 
    
    } else{
      printf("ERRORE: messaggio strano in task_handle_handshake\n");
      print_msg_struct(msg);
      vTaskDelay(pdMS_TO_TICKS(5000));
    }

    //TODO test 
    if(BLINK_LOOP_IF_IDS_ARE_KNOWN){
      if(SELF_ID == ROOT_ID && hello_msg_from_slave_recived){
        resume_loop_blink();
      }else if(hello_msg_from_master_recived && hello_msg_from_slave_recived){
        resume_loop_blink();
      }

    }
    //TODO

    if(hello_msg_from_master_recived && hello_msg_from_slave_recived){ //(hello_msg_from_master_recived && hello_msg_from_slave_recived){
      send_handshake_type_report_to_root();
      report_msg_to_root_sent = true;
      // vTaskDelay(2000);
      vTaskDelete(nullptr);
    }
  }
}


void task_send_hello_msg_to_master(void *arg){ 
  if(SELF_ID == ROOT_ID){
    vTaskDelete(nullptr);
  }
  
  while (!hello_msg_from_master_recived){
    Payload payload;
    payload.payload_handshake.handshake_type = type_hello;
    Msg* handshake_msg = create_msg(SELF_ID, -1, type_handshake, payload);

    // xQueueSend(h_queue_send_to_master, &handshake_msg, portMAX_DELAY);
    send_msg_to_master(handshake_msg);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  printf("hello_msg_from_master_recived; UCCIDO LA TASK\n");
  vTaskDelete(nullptr);
}


void task_handle_report(void* arg){
  if(SELF_ID != ROOT_ID){ //it shouldn't be the case.
    vTaskDelete(nullptr);
  }

  while(1){
  Msg* msg = nullptr;
  xQueueReceive(h_queue_report, &msg, portMAX_DELAY);
    if(BLINK_LOOP_IF_RECEIVED_REPORT && msg->type == type_report){
      resume_loop_blink();
    }
    printf("REPORT: SLAVE=%d, MY=%d, MASTER=%d\n", msg->payload.payload_report.my_slave_id, msg->payload.payload_report.my_id, msg->payload.payload_report.my_master_id);
    recive_new_report(msg->payload.payload_report);
  }
}



//todo STO RISCRIVENDO COMPLETAMENTE QUESTO FILE QUA SOTTO, ANCHE HANDLER REPORT é DA RIVEDERE

// // FIX: Added 'volatile' for variables shared between tasks
// volatile int last_MtS_ack_sender_id = -1;
// volatile bool received_MtS_ack = false;

// volatile int last_StM_ack_sender_id = -1;
// volatile bool received_StM_ack = false;

// void task_ping_slave(){ // mando MtS a slave
//   Payload p;
//   p.payload_handshake.handshake_type = type_MtS;
//   Msg* msg = create_msg(SELF_ID, UNKNOWN_ID, type_handshake, p); 

//   // FIX: Reset flag BEFORE sending to avoid race conditions
//   received_MtS_ack = false; 
//   send_msg_to_slave(msg);

//   vTaskDelay(pdMS_TO_TICKS(1000));
  
//   if(received_MtS_ack){ // slave esiste
//     if(last_MtS_ack_sender_id != SLAVE_ID){ // è diverso da slave ID
//       printf(">>> SLAVE CHANGED FROM %d TO %d\n", SLAVE_ID, last_MtS_ack_sender_id);

//       SLAVE_ID = last_MtS_ack_sender_id; // FIX: changed '==' to '='
//       send_buffered_messages_to_slave();
//       //todo send_report_to_root()
//     }
//   } else { // slave non esiste
//     printf(">>> SLAVE %d DOESNT RESPOND, I ASSUME HE ISNT THERE (SLAVE_ID = -1)\n", SLAVE_ID);
//     SLAVE_ID = UNKNOWN_ID; // FIX: changed '==' to '='
//     //todo send_report_to_root()
//   }
// }

// // COMPLETED: Implemented master ping task
// void task_ping_master(){ 
//   Payload p;
//   p.payload_handshake.handshake_type = type_StM;
//   Msg* msg = create_msg(SELF_ID, UNKNOWN_ID, type_handshake, p); 

//   received_StM_ack = false; 
//   send_msg_to_master(msg);

//   vTaskDelay(pdMS_TO_TICKS(1000));
  
//   if(received_StM_ack){ // master esiste
//     if(last_StM_ack_sender_id != MASTER_ID){ // è diverso da master ID
//       printf(">>> MASTER CHANGED FROM %d TO %d\n", MASTER_ID, last_StM_ack_sender_id);

//       MASTER_ID = last_StM_ack_sender_id; 
//       // todo send_buffered_messages_to_master() se necessario
//       // todo send_report_to_root()
//     }
//   } else { // master non esiste
//     printf(">>> MASTER %d DOESNT RESPOND, I ASSUME HE ISNT THERE (MASTER_ID = -1)\n", MASTER_ID);
//     MASTER_ID = UNKNOWN_ID; 
//     // todo send_report_to_root()
//   }
// }


// void task_handle_handshakes_ciaooooo(){ //! TOGLI IL CIAOOOO
//   while(1){
//     Msg *msg = nullptr;
//     xQueueReceive(h_queue_handshake, &msg, portMAX_DELAY);

//     if (msg == nullptr) continue; // Safety check

//     if(msg->payload.payload_handshake.handshake_type == type_MtS){ // il master fa ciao rispondigli
//       if(msg->sender_id != MASTER_ID){
//         printf(">>> MASTER CHANGED FROM %d TO %d\n", MASTER_ID, msg->sender_id);
//         MASTER_ID = msg->sender_id; // FIX: changed '==' to '='
//         //todo send_report_to_root()
//       }

//       Payload p;
//       p.payload_handshake.handshake_type = type_MtS_ack;
//       Msg* nm = create_msg(SELF_ID, UNKNOWN_ID, type_handshake, p);
//       send_msg_to_master(nm);

//     } else if(msg->payload.payload_handshake.handshake_type == type_MtS_ack){
//       last_MtS_ack_sender_id = msg->sender_id;
//       received_MtS_ack = true; // FIX: consistency (true instead of 1)

//     } else if(msg->payload.payload_handshake.handshake_type == type_StM){
//       if(msg->sender_id != SLAVE_ID){
//         printf(">>> SLAVE CHANGED FROM %d TO %d\n", SLAVE_ID, msg->sender_id);
//         SLAVE_ID = msg->sender_id; // FIX: changed '==' to '='
//         //todo send_report_to_root()
//       }

//       Payload p;
//       p.payload_handshake.handshake_type = type_StM_ack;
//       Msg* nm = create_msg(SELF_ID, UNKNOWN_ID, type_handshake, p);
//       send_msg_to_slave(nm);

//     } else if(msg->payload.payload_handshake.handshake_type == type_StM_ack){
//       // COMPLETED: Handled StM_ack logic
//       last_StM_ack_sender_id = msg->sender_id;
//       received_StM_ack = true; 
//     }

//     free(msg);  // OR delete msg; OR vPortFree(msg); depending on your system.
//   }
// }