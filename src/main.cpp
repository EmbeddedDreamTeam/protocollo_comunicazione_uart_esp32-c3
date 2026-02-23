#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_task_wdt.h"

#include "structs.h"

//* _______________________________________ CONSTS e STRUCTS

#define U_WITH_MASTER 0
#define U_WITH_SLAVE 1

#define FROM_MASTER_RX 9
#define TO_MASTER_TX 10
#define FROM_SLAVE_RX 2
#define TO_SLAVE_TX 3

#define U_BUF_SIZE 1024 
#define BAUD_RATE 115200

#define LED_GPIO GPIO_NUM_8 

//ids
#define ROOT_ID 0 
#define UNKNOWN_ID -1

//header e footer di ogni messaggio
#define HEADER_BYTE 0xAA
#define FOOTER_4_BYTES 0xCAFEBABE

#define STR_PROVA "messaggio_corretto"

int MASTER_ID = UNKNOWN_ID; 
int SELF_ID = UNKNOWN_ID;
int SLAVE_ID = UNKNOWN_ID;

bool BLINK_ON_RECEIVE_MSG = false; 
bool BLINK_ON_SEND_MSG = false;

//* _______________________________________FRAMEWORK GLOBALS
//handles:
TaskHandle_t h_task_blink_led;

//code x tutti i tipi di comandi diversi
QueueHandle_t h_queue_command_01;
QueueHandle_t h_queue_command_02;
QueueHandle_t h_queue_handshake;

//code x inviare messaggi
QueueHandle_t h_queue_send_to_slave;
QueueHandle_t h_queue_send_to_master;

typedef struct {
  uart_port_t select_uart; 
  QueueHandle_t select_queue;
} InfoUART;

//* _______________________________________LED
void toggle_led(bool s){
  gpio_set_level(LED_GPIO, !s);
}

void init_led(){
  gpio_reset_pin(LED_GPIO);
  gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
  toggle_led(0);
}

void task_blink_led(void *arg){
  int DELAY = 200;
  while(1){
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    toggle_led(1);
    vTaskDelay(pdMS_TO_TICKS(DELAY));
    toggle_led(0);
  }
}

int L_DELAY = 350;
void task_led(void *info){
  init_led();
  
  while(1){
    toggle_led(0);
    vTaskDelay(pdMS_TO_TICKS(L_DELAY));
    toggle_led(1);
    vTaskDelay(pdMS_TO_TICKS(L_DELAY));
  }
}

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

//* _______________________________________UART RECEIVE
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

//* _______________________________________ EXECUTE COMMANDS
void task_execute_command_01(void *arg){
  while(1){
    Msg *msg = nullptr;
    xQueueReceive(h_queue_command_01, &msg, portMAX_DELAY);
    printf("START: execute_command_01\n");
    vTaskDelay(pdMS_TO_TICKS(7000));
    printf("END: execute_command_01\n");
    delete msg; 
  }
}

void task_execute_command_02(void *arg){
  while(1){
    Msg *msg = nullptr;
    xQueueReceive(h_queue_command_02, &msg, portMAX_DELAY);
    printf("START: execute_command_02\n");
    vTaskDelay(pdMS_TO_TICKS(9000));
    printf("END: execute_command_02\n");
    delete msg; 
  }
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

  xQueueSend(h_queue_send_to_slave, &prova, portMAX_DELAY); 
}


extern "C" void app_main(void){
  esp_task_wdt_deinit();

  SELF_ID = 0; 
  bool SET_DEFAULT_IDS = true;
  bool TEST_FUN = true;
  BLINK_ON_RECEIVE_MSG = 1; 
  BLINK_ON_SEND_MSG = 1;

  if(SELF_ID == 0){ 
    L_DELAY = 3000;
    MASTER_ID = -1;
    if(SET_DEFAULT_IDS) SLAVE_ID = 1;
  }else if(SELF_ID == 1){
    L_DELAY = 600;
    if(SET_DEFAULT_IDS){
      MASTER_ID = 0;
      SLAVE_ID = 2;
    }
  }else if(SELF_ID == 2){
    L_DELAY = 100;
    if(SET_DEFAULT_IDS){
      MASTER_ID = 2;
      SLAVE_ID = -1;
    }
  }

  vTaskDelay(pdMS_TO_TICKS(1500)); 

  h_queue_command_01 = xQueueCreate(10, sizeof(Msg*));
  h_queue_command_02 = xQueueCreate(10, sizeof(Msg*));
  h_queue_handshake = xQueueCreate(10, sizeof(Msg*));
  h_queue_send_to_slave = xQueueCreate(10, sizeof(Msg*));
  h_queue_send_to_master = xQueueCreate(10, sizeof(Msg*));

  init_uart((uart_port_t)U_WITH_SLAVE, FROM_SLAVE_RX, TO_SLAVE_TX);
  init_uart((uart_port_t)U_WITH_MASTER, FROM_MASTER_RX, TO_MASTER_TX);

  init_led();
  if(BLINK_ON_RECEIVE_MSG || BLINK_ON_SEND_MSG){ 
    xTaskCreate(task_blink_led, "task_blink_led", 2048, nullptr, 24, &h_task_blink_led);
  }else{
    xTaskCreate(task_led, "task_led", 2048, nullptr, 2, nullptr);
  }

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

  if(!SET_DEFAULT_IDS){
    xTaskCreate(task_handle_handshake, "task_handle_handshake", 2048, nullptr, 2, nullptr);
    xTaskCreate(task_send_hello_msg_to_master, "task_send_hello_msg_to_master", 2048, nullptr, 2, nullptr);
  }

  xTaskCreate(task_execute_command_01, "task_execute_command_01", 5000, nullptr, 1, nullptr);
  xTaskCreate(task_execute_command_02, "task_execute_command_02", 5000, nullptr, 1, nullptr);

  int ct = 0;
  while(SELF_ID == 0 && TEST_FUN){
    test(ct);
    ct++;
    vTaskDelay(pdMS_TO_TICKS(10000));
  }

  while(1) { 
    vTaskDelay(pdMS_TO_TICKS(10000)); 
  }
}