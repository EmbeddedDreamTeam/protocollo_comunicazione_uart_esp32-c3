#include <stdio.h>
#include <string.h>

//definiti da me:
#include "msg_structs.h"
#include "utils_communication.h"

//* _______________________________________UART RECEIVE
void sort_new_msg(Msg *msg){
  if(msg->type == type_command_01){
    xQueueSend(h_queue_command_01, &msg, portMAX_DELAY);
  }else if (msg->type == type_command_02){
    xQueueSend(h_queue_command_02, &msg, portMAX_DELAY);
  }else if (msg->type == type_handshake){
    xQueueSend(h_queue_handshake, &msg, portMAX_DELAY);
  }else{
    printf("ERRORE: type: %i , non esiste\n", msg->type);
  }
}

void task_receive_uart(void *arg){
  InfoUART* info_uart = (InfoUART*) arg;

  while (1){
    Msg *msg = new Msg(); 

    bool message_ok = false;
    while (!message_ok){
      message_ok = false;
      uart_read_bytes(info_uart->select_uart, (uint8_t*)msg, 1, portMAX_DELAY);

      if(msg->header != HEADER_BYTE){
        continue; 
      }
      uart_read_bytes(info_uart->select_uart, (uint8_t*)msg+1, sizeof(Msg)-1, portMAX_DELAY);
      if(msg->footer == FOOTER_4_BYTES){
        message_ok = true;
      }
    }

    if(BLINK_ON_RECEIVE_MSG && msg->header == HEADER_BYTE && msg->footer == FOOTER_4_BYTES){
      xTaskNotifyGive(h_task_blink_led);
    }

    printf("\n====================\n");
    
    const char* role = get_role_name(info_uart->select_uart); 
    if(msg->target_id == SELF_ID || msg->target_id == -1){
      printf("SONO: %d, HO RICEVUTO DA: %s, il messggio E' PER ME (non verra' ritrasmesso):\n", SELF_ID, role);
      print_msg_struct(msg);
      sort_new_msg(msg); 

    }else{ 
      int uart_opposta = (int)!(bool)info_uart->select_uart;
      const char* tpr = get_role_name(uart_opposta); 

      printf("SONO: %d, HO RICEVUTO DA: %s, il messaggio NON E' PER ME\n", SELF_ID, role);
      if(!(((int)uart_opposta == (int)UART_NUM_1 && SLAVE_ID == -1) || ((int)uart_opposta == (int)UART_NUM_0 && MASTER_ID == -1))){
        printf("VERRA' RITRASMESSO A: %s\n", tpr);
        print_msg_struct(msg);
        xQueueSend(info_uart->select_queue, &msg, portMAX_DELAY); 

      }else{
        printf("ERRORE: IL DESTINATARIO NON ESISTE\n");
        print_msg_struct(msg);
        delete msg; 
      }
    printf("====================\n");
    fflush(stdout);
    }
  }
  delete info_uart; 
}

//* _______________________________________UART SEND
void task_send_uart(void *arg){
  InfoUART* info_uart = (InfoUART*) arg;
  print_info_uart_struct(info_uart);

  while (1) {
    Msg *msg = nullptr; 
    xQueueReceive(info_uart->select_queue, &msg, portMAX_DELAY);

    printf("\n====================\n");
    const char* role = get_role_name(info_uart->select_uart);
    printf("SONO: %d, INVIO A: %s, IL SEGUENTE MESSAGGIO:\n", SELF_ID, role);
    print_msg_struct(msg);

    uart_write_bytes(info_uart->select_uart, (const void*)msg, sizeof(Msg));

    if(BLINK_ON_SEND_MSG){
      xTaskNotifyGive(h_task_blink_led);
    }
    printf("====================\n");

    delete msg; 
  }
  delete info_uart; 
}



//* _______________________________________ ON START INIT UART
void init_uart(uart_port_t uart_num, int rx_pin, int tx_pin) {
    const uart_config_t uart_config = {
        .baud_rate = BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122, 
        .source_clk = UART_SCLK_APB,
    };
    
    uart_driver_install(uart_num, U_BUF_SIZE * 2, 0, 0, nullptr, 0);
    uart_param_config(uart_num, &uart_config);
    uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    gpio_set_pull_mode((gpio_num_t)rx_pin, GPIO_PULLUP_ONLY); 

    if(SELF_ID == 1){ 
      printf("\nUART: %d, tx_pin: %d, rx_pin: %d\n", uart_num, tx_pin, rx_pin);
    }
}