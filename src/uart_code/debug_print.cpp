#include <stdio.h>
#include <string.h>

//definiti da me:
#include "msg_structs.h"
#include "utils_communication.h"


//* _______________________________________PRINT DEBUG INFO
void print_info_uart_struct(InfoUART* info){
  printf("\nselect_uart: %d\n", info->select_uart);
  printf("select_queue: %p\n\n", (void*)info->select_queue);
  fflush(stdout);
}


const char* get_role_name(int role) {
    if (role == 0) {
        return "MASTER";
    } else if (role == 1) {
        return "SLAVE";
    } else {
        return "UNKNOWN";
    }
}


void print_msg_struct(Msg* msg){
  printf("PRINTING STRUCT MESSAGE:\n");
  printf("HEAP PT: %p \n", (void*)msg); 
  printf("Sender: %d\n", msg->sender_id);
  printf("Target: %d\n", msg->target_id);
  printf("Type [0:type_command_01, 1:type_command_02, 2:type_handshake, 3:payload_report]: %d\n", msg->type);
  printf("Header: %d\n", msg->header);
  printf("Footer: %ld\n", msg->footer);

  if (msg->type == type_command_01) {
    printf("str1: %s\n", msg->payload.payload_command_01.str1);
    printf("str2: %s\n", msg->payload.payload_command_01.str2);
  } else if (msg->type == type_command_02) {
    printf("num1: %d\n", msg->payload.payload_command_02.num1);
    printf("num2: %f\n", msg->payload.payload_command_02.num2);
  } else if (msg->type == type_handshake) {
    printf("Type [0:type_hello, 1:type_ACK_hello]: %d\n", msg->payload.payload_handshake.handshake_type);
  } else if (msg->type == type_report) {
    printf("my_slave_id: %d\n", msg->payload.payload_report.my_slave_id);
    printf("my_id: %d\n", msg->payload.payload_report.my_id);
    printf("my_master_id: %d\n", msg->payload.payload_report.my_master_id);
  } else{
    printf("ERRORE: type non riconosciuto\n");
  }
  printf("\n");
}