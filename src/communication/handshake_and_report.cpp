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
  Msg* hello_msg = new Msg(); 
  hello_msg->sender_id = SELF_ID;
  hello_msg->target_id = SLAVE_ID;
  hello_msg->type = type_handshake;
  hello_msg->payload.payload_handshake.handshake_type = type_ACK_hello;
  hello_msg->header = HEADER_BYTE;
  hello_msg->footer = FOOTER_4_BYTES;

  xQueueSend(h_queue_send_to_slave, &hello_msg, portMAX_DELAY);
  printf("DID I SEND HELLO?\n");
}

void send_handshake_type_report_to_root(){
  if(SELF_ID == ROOT_ID){
    vTaskDelete(nullptr);
  }
  
  Msg* hello_msg = new Msg(); 
  hello_msg->sender_id = SELF_ID;
  hello_msg->target_id = ROOT_ID;
  hello_msg->type = type_handshake;

  hello_msg->payload.payload_report.my_id = SELF_ID;
  hello_msg->payload.payload_report.my_slave_id = SLAVE_ID;
  hello_msg->payload.payload_report.my_master_id = MASTER_ID;

  hello_msg->header = HEADER_BYTE;
  hello_msg->footer = FOOTER_4_BYTES;

  xQueueSend(h_queue_send_to_master, &hello_msg, portMAX_DELAY);
}

void task_handle_handshake(void *arg){ 
  while(1){
    Msg *msg = nullptr;
    xQueuePeek(h_queue_handshake, &msg, portMAX_DELAY);

    if(msg->type == type_handshake && msg->payload.payload_handshake.handshake_type == type_hello){
      xQueueReceive(h_queue_handshake, &msg, 0);
      SLAVE_ID = msg->sender_id;
      hello_msg_from_slave_recived = true;
      send_handshake_msg_to_slave();
      delete msg; 

    } else if(msg->type == type_handshake && msg->payload.payload_handshake.handshake_type == type_ACK_hello){
      xQueueReceive(h_queue_handshake, &msg, 0);
      MASTER_ID = msg->sender_id;
      hello_msg_from_master_recived = true;
      delete msg; 
    
    } else{
      printf("ERRORE: messaggio strano in task_handle_handshake\n");
      print_msg_struct(msg);
      vTaskDelay(pdMS_TO_TICKS(5000));
    }

    if(hello_msg_from_master_recived && hello_msg_from_slave_recived){
      send_handshake_type_report_to_root();
      report_msg_to_root_sent = true;
      vTaskDelete(nullptr);
    }
  }
}

void task_send_hello_msg_to_master(void *arg){ 
  if(SELF_ID == ROOT_ID){
    vTaskDelete(nullptr);
  }
  
  while (!hello_msg_from_master_recived){
    Msg* hello_msg = new Msg();

    hello_msg->header = HEADER_BYTE;
    hello_msg->footer = FOOTER_4_BYTES;
    hello_msg->sender_id = SELF_ID;
    hello_msg->target_id = -1;
    hello_msg->type = type_handshake;
    hello_msg->payload.payload_handshake.handshake_type = type_hello;

    xQueueSend(h_queue_send_to_master, &hello_msg, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
  printf("hello_msg_from_master_recived; UCCIDO LA TASK\n");
  vTaskDelete(nullptr);
}