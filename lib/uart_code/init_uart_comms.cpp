#include "utils_uart_comms.h"

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


void task_loop_print_ids_array(void* info){
  while (1){
    print_ids_array();
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}


void init_uart_comms(){
  
  //todo for test
  
  //!!!ID!!!
  SELF_ID = 2; 

  //*LOGS
  SHOW_UART_COMMS_LOGS = 1;
  bool LOOP_PRINT_IDS_ARRAY = 0;

  //*TEST
  bool USE_DEFAULT_IDS = 0; //! == 1 => NON INIZIALIZZA la logica di handshake
  bool TEST_FUN = 0;
  int INIT_PAUSE = 1500;

  //*UART
  PRINT_RECEIVED_BYTES = 0;

  //*LED
  BLINK_ON_RECEIVE_MSG = 1;
  BLINK_ON_SEND_MSG = 1;

  int32_t LED_LOOP_DELAY = 5000;

  if(SELF_ID == 0){ 
    MASTER_ID = -1;

    if(USE_DEFAULT_IDS) SLAVE_ID = 1;
  }else if(SELF_ID == 1){

    if(USE_DEFAULT_IDS){
      MASTER_ID = 0;
      SLAVE_ID = 2;
    }
  }else if(SELF_ID == 2){
    if(USE_DEFAULT_IDS){
      MASTER_ID = 2;
      SLAVE_ID = -1;
    }
  }

  //todo for test


  // esp_task_wdt_deinit();
  vTaskDelay(pdMS_TO_TICKS(INIT_PAUSE)); //!ATTENTO - RIMUOVERE O SETTARE A 1500
  if(SHOW_UART_COMMS_LOGS)
    printf("\nSTARTED init_uart_comms - STARTED init_uart_comms - STARTED init_uart_comms - STARTED init_uart_comms\n");

  //*LED SETUP
  init_led();
  set_loop_blink_delay(LED_LOOP_DELAY);
  //*LED DEFAULT BEHAVIOUR IS BLINKING IF NOTHING ELSE
  if(!BLINK_ON_RECEIVE_MSG){
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
  init_uart_mutexes();
  init_uart((uart_port_t)U_WITH_SLAVE, FROM_SLAVE_RX, TO_SLAVE_TX);
  init_uart((uart_port_t)U_WITH_MASTER, FROM_MASTER_RX, TO_MASTER_TX);

  xTaskCreate(task_receive_uart, "task_receive_uart_master", 10000, (void*)U_WITH_MASTER, 2, nullptr);
  xTaskCreate(task_receive_uart, "task_receive_uart_slave", 10000, (void*)U_WITH_SLAVE, 2, nullptr);

  xTaskCreate(task_send_uart, "task_send_uart_master", 10000, (void*)U_WITH_MASTER, 2, nullptr);
  xTaskCreate(task_send_uart, "task_send_uart_slave", 10000, (void*)U_WITH_SLAVE, 2, nullptr);

  //todo E' SOLO UN MOCKUP, DA RIMUOVERE 
  xTaskCreate(task_execute_command_01, "task_execute_command_01", 5000, nullptr, 1, nullptr);
  xTaskCreate(task_execute_command_02, "task_execute_command_02", 5000, nullptr, 1, nullptr);
  //xTaskCreate(task_execute_servo, "task_execute_servo", 5000, nullptr, 2, nullptr);
  //todo

  //*HANDSHAKE
  int arr[3] = {0,1,2};
  init_report_handler(arr, 3, USE_DEFAULT_IDS);
  if(!USE_DEFAULT_IDS){
    xTaskCreate(task_ping_slave, "task_ping_slave", 5000, nullptr, 2, nullptr);
    xTaskCreate(task_ping_master, "task_ping_master", 5000, nullptr, 2, nullptr);
    xTaskCreate(task_handle_handshakes, "task_handle_handshakes", 5000, nullptr, 24, nullptr);
    xTaskCreate(task_handle_report, "task_handle_report", 5000, nullptr, 2, nullptr);
  }
  

  if(SELF_ID == ROOT_ID && LOOP_PRINT_IDS_ARRAY){
    xTaskCreate(task_loop_print_ids_array, "task_loop_print_ids_array", 2000, nullptr, 5, nullptr);
  }

  //todo TEST FUNCTION
  if(SELF_ID == ROOT_ID && TEST_FUN){
    xTaskCreate(test_task, "test_task", 5000, nullptr, 5, nullptr);
  }

}
