#include "utils_communication.h"

//* _______________________________________ MAIN e TEST
void test(int num){
  Msg* prova = new Msg(); 
  prova->header = HEADER_BYTE;
  prova->footer = FOOTER_4_BYTES;
  prova->sender_id = 0;
  prova->target_id = 2;
  prova->type = type_command_01;

  char* s1 = prova->payload.payload_command_01.str1;
  char* s2 = prova->payload.payload_command_01.str2;
  sprintf(s1, "MGSN: %d", num);
  strcpy(s2, STR_PROVA);

  printf("\nmetto in h_queue_send_to_slave: %p\n", (void*)prova);

  // xQueueSend(h_queue_send_to_slave, &prova, portMAX_DELAY); 
  send_msg_to_slave(prova);
}


void init_comunication(){
  
  //todo for test
  
  SELF_ID = 0; 
  bool SET_DEFAULT_IDS = 0; //OTHERWISE IT ATTEMPTS TO SEND HANDSHAKES
  bool TEST_FUN = 0;
  BLINK_ON_RECEIVE_MSG = 1; 
  BLINK_ON_SEND_MSG = 1;
  BLINK_LOOP_WHEN_IF_IDS_ARE_KNOWN = 1;

  int32_t L_DELAY = 5000;

  if(SELF_ID == 0){ 
    MASTER_ID = -1;
    if(SET_DEFAULT_IDS) SLAVE_ID = 1;
  }else if(SELF_ID == 1){
    if(SET_DEFAULT_IDS){
      MASTER_ID = 0;
      SLAVE_ID = 2;
    }
  }else if(SELF_ID == 2){
    if(SET_DEFAULT_IDS){
      MASTER_ID = 2;
      SLAVE_ID = -1;
    }
  }

  //todo for test


  esp_task_wdt_deinit();
  vTaskDelay(pdMS_TO_TICKS(15000)); 
  printf("\nSTART START START START START START START START!!!\n");

  init_led();
  set_loop_blink_delay(L_DELAY);
  if(!BLINK_ON_RECEIVE_MSG && !BLINK_ON_RECEIVE_MSG){
    resume_loop_blink();
  }

  h_queue_command_01 = xQueueCreate(10, sizeof(Msg*));
  h_queue_command_02 = xQueueCreate(10, sizeof(Msg*));
  h_queue_handshake = xQueueCreate(10, sizeof(Msg*));
  h_queue_send_to_slave = xQueueCreate(10, sizeof(Msg*));
  h_queue_send_to_master = xQueueCreate(10, sizeof(Msg*));
  h_queue_report = xQueueCreate(10, sizeof(Msg*));

  init_uart((uart_port_t)U_WITH_SLAVE, FROM_SLAVE_RX, TO_SLAVE_TX);
  init_uart((uart_port_t)U_WITH_MASTER, FROM_MASTER_RX, TO_MASTER_TX);


  InfoUART* info_receive_master = new InfoUART(); 
  info_receive_master->select_uart = (uart_port_t)U_WITH_MASTER;
  info_receive_master->select_queue = h_queue_send_to_slave;
  xTaskCreate(task_receive_uart, "task_receive_uart_master", 5000, (void*)info_receive_master, 2, nullptr);

  InfoUART* info_receive_slave = new InfoUART();
  info_receive_slave->select_uart = (uart_port_t)U_WITH_SLAVE;
  info_receive_slave->select_queue = h_queue_send_to_master;
  xTaskCreate(task_receive_uart, "task_receive_uart_slave", 5000, (void*)info_receive_slave, 2, nullptr);

  InfoUART* info_send_master = new InfoUART(); 
  info_send_master->select_uart = (uart_port_t)U_WITH_MASTER;
  info_send_master->select_queue = h_queue_send_to_master;
  xTaskCreate(task_send_uart, "task_send_uart_master", 5000, (void*)info_send_master, 2, nullptr);

  InfoUART* info_send_slave = new InfoUART(); 
  info_send_slave->select_uart = (uart_port_t)U_WITH_SLAVE;
  info_send_slave->select_queue = h_queue_send_to_slave;
  xTaskCreate(task_send_uart, "task_send_uart_slave", 5000, (void*)info_send_slave, 2, nullptr);

  xTaskCreate(task_execute_command_01, "task_execute_command_01", 5000, nullptr, 1, nullptr);
  xTaskCreate(task_execute_command_02, "task_execute_command_02", 5000, nullptr, 1, nullptr);

  if(!SET_DEFAULT_IDS){
    xTaskCreate(task_handle_handshake, "task_handle_handshake", 2048, nullptr, 2, nullptr);
    xTaskCreate(task_send_hello_msg_to_master, "task_send_hello_msg_to_master", 2048, nullptr, 2, nullptr);
    xTaskCreate(task_handle_report, "task_handle_report", 2028, nullptr, 2, nullptr);
  }


  // while(1){
  //   printf("SLAVE: %d\n", SLAVE_ID);
  //   vTaskDelay(pdMS_TO_TICKS(4000));
  // }
  

  
  //todo for test
  int ct = 0;
  while(SELF_ID == 0 && TEST_FUN){
    test(ct);
    ct++;
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
  //todo
}
