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
  // Msg* hello_msg = new Msg(); 
  // hello_msg->sender_id = SELF_ID;
  // hello_msg->target_id = SLAVE_ID;
  // hello_msg->type = type_handshake;
  // hello_msg->payload.payload_handshake.handshake_type = type_ACK_hello;
  // hello_msg->header = HEADER_BYTE;
  // hello_msg->footer = FOOTER_4_BYTES;

  Payload p;
  p.payload_handshake.handshake_type = type_ACK_hello;
  Msg* hello_msg = create_msg(SELF_ID, SLAVE_ID, type_handshake, p); 

  // xQueueSend(h_queue_send_to_slave, &hello_msg, portMAX_DELAY);
  send_msg_to_slave(hello_msg);
}

void send_handshake_type_report_to_root(){
  if(SELF_ID == ROOT_ID){
    vTaskDelete(nullptr);
  }
  
  Payload p;
  p.payload_report.my_id = SELF_ID;
  p.payload_report.my_master_id = MASTER_ID;
  p.payload_report.my_slave_id = SLAVE_ID;
  Msg* report_msg = create_msg(SELF_ID, ROOT_ID, type_report, p);

  // xQueueSend(h_queue_send_to_master, &report_msg, portMAX_DELAY);
  send_msg_to_master(report_msg);
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

    //TODO test 
    if(BLINK_LOOP_WHEN_IF_IDS_ARE_KNOWN){
      if(SELF_ID == ROOT_ID && hello_msg_from_slave_recived){
        resume_loop_blink();
      }else if(hello_msg_from_master_recived && hello_msg_from_slave_recived){
        resume_loop_blink();
      }

    }
    //TODO

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
    Payload payload;
    payload.payload_handshake.handshake_type = type_hello;
    Msg* handshake_msg = create_msg(SELF_ID, -1, type_handshake, payload);

    // xQueueSend(h_queue_send_to_master, &handshake_msg, portMAX_DELAY);
    send_msg_to_master(handshake_msg);
    vTaskDelay(pdMS_TO_TICKS(2000));
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
    printf("REPORT: SLAVE=%d, MY=%d, MASTER=%d\n", msg->payload.payload_report.my_slave_id, msg->payload.payload_report.my_id, msg->payload.payload_report.my_master_id);
    // todo ALL THE LOGIC
  }
}