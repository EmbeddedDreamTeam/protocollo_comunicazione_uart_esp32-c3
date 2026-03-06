#include "utils_communication.h"

//* _______________________________________ MAIN e TEST
void test_task(void* info){

  int ct =0;
  while(1){
    Msg* prova = new Msg(); 
    prova->header = HEADER_BYTE;
    prova->footer = FOOTER_4_BYTES;
    prova->sender_id = 0;
    prova->target_id = 2;
    prova->type = type_command_01;

    char* s1 = prova->payload.payload_command_01.str1;
    char* s2 = prova->payload.payload_command_01.str2;
    sprintf(s1, "MGSN: %d", ct);
    strcpy(s2, STR_PROVA);

    printf("\nmetto in h_queue_send_to_slave: %p\n", (void*)prova);

    send_msg_to_slave(prova);

    vTaskDelay(pdMS_TO_TICKS(5000));
    ct++;
  }
}


void init_comunication(){
  
  //todo for test
  
  SELF_ID = 0; 
  bool SET_DEFAULT_IDS = 0; //OTHERWISE IT ATTEMPTS TO SEND HANDSHAKES
  bool TEST_FUN = 0;

  PRINT_RECEIVED_BYTES = 0;

  int32_t L_DELAY = 5000;

  if(SELF_ID == 0){ 
    MASTER_ID = -1;

    BLINK_LOOP_IF_IDS_ARE_KNOWN = 0;
    BLINK_LOOP_IF_RECEIVED_REPORT = 1;

    if(SET_DEFAULT_IDS) SLAVE_ID = 1;
  }else if(SELF_ID == 1){

    BLINK_LOOP_IF_IDS_ARE_KNOWN = 1;
    BLINK_LOOP_IF_RECEIVED_REPORT = 0;

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


  // esp_task_wdt_deinit();
  vTaskDelay(pdMS_TO_TICKS(15000)); //!ATTENTO - RIMUOVERE O SETTARE A 1500
  printf("\nSTART START START START START START START START!!!\n");

  //*LED SETUP
  init_led();
  set_loop_blink_delay(L_DELAY);
  //*LED DEFAULT BEHAVIOUR IS BLINKING IF NOTHING ELSE
  if(!BLINK_ON_RECEIVE_MSG && !BLINK_ON_RECEIVE_MSG){
    resume_loop_blink();
  }

  //*QUEUES
  h_queue_command_01 = xQueueCreate(10, sizeof(Msg*));
  h_queue_command_02 = xQueueCreate(10, sizeof(Msg*));
  h_queue_handshake = xQueueCreate(10, sizeof(Msg*));
  h_queue_send_to_slave = xQueueCreate(10, sizeof(Msg*));
  h_queue_send_to_master = xQueueCreate(10, sizeof(Msg*));
  h_queue_report = xQueueCreate(10, sizeof(Msg*));
  h_queue_servo = xQueueCreate(10, sizeof(Msg*));

  //*UART
  init_uart((uart_port_t)U_WITH_SLAVE, FROM_SLAVE_RX, TO_SLAVE_TX);
  init_uart((uart_port_t)U_WITH_MASTER, FROM_MASTER_RX, TO_MASTER_TX);

  xTaskCreate(task_receive_uart, "task_receive_uart_master", 5000, (void*)U_WITH_MASTER, 2, nullptr);
  xTaskCreate(task_receive_uart, "task_receive_uart_slave", 5000, (void*)U_WITH_SLAVE, 2, nullptr);

  xTaskCreate(task_send_uart, "task_send_uart_master", 5000, (void*)U_WITH_MASTER, 2, nullptr);
  xTaskCreate(task_send_uart, "task_send_uart_slave", 5000, (void*)U_WITH_SLAVE, 2, nullptr);

  //todo E' SOLO UN MOCKUP, DA RIMUOVERE 
  xTaskCreate(task_execute_command_01, "task_execute_command_01", 5000, nullptr, 1, nullptr);
  xTaskCreate(task_execute_command_02, "task_execute_command_02", 5000, nullptr, 1, nullptr);
  xTaskCreate(task_execute_servo, "task_execute_servo", 5000, nullptr, 2, nullptr);
  //todo

  //*HANDSHAKE
  if(!SET_DEFAULT_IDS){
    xTaskCreate(task_handle_handshake, "task_handle_handshake", 5000, nullptr, 2, nullptr);
    xTaskCreate(task_send_hello_msg_to_master, "task_send_hello_msg_to_master", 5000, nullptr, 2, nullptr);
    xTaskCreate(task_handle_report, "task_handle_report", 5000, nullptr, 2, nullptr);
  }


  //! REMOVE
  // float rr[3] = {1,2,3};
  // convert_servo_instructions(rr, 3);
  

  //todo TEST FUNCTION
  if(SELF_ID == 0 && TEST_FUN){
    xTaskCreate(test_task, "test_task", 5000, nullptr, 5, nullptr);
  }


}
