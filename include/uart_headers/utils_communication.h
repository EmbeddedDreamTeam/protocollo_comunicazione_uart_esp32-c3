#pragma once

#include <stdio.h>
#include <string.h>
#include <queue>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_task_wdt.h"
#include "freertos/semphr.h"

//mine
#include "msg_structs.h"

using namespace std;



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


typedef struct {
  uart_port_t select_uart; 
  QueueHandle_t select_queue;
} InfoUART;


//* GLOBAL_VARS.CPP
extern int MASTER_ID; 
extern int SELF_ID;
extern int SLAVE_ID;

extern bool BLINK_ON_RECEIVE_MSG; 
extern bool BLINK_ON_SEND_MSG;
extern bool BLINK_LOOP_IF_IDS_ARE_KNOWN;
extern bool BLINK_LOOP_IF_RECEIVED_REPORT;
extern bool PRINT_RECEIVED_BYTES;

//handles:
// extern TaskHandle_t h_task_blink_led_once;

//code x tutti i tipi di comandi diversi
extern QueueHandle_t h_queue_command_01;
extern QueueHandle_t h_queue_command_02;
extern QueueHandle_t h_queue_handshake;
extern QueueHandle_t h_queue_report;
extern QueueHandle_t h_queue_servo;

//code x inviare messaggi
extern QueueHandle_t h_queue_send_to_slave;
extern QueueHandle_t h_queue_send_to_master;


//* INIT_COMUNICATION.CPP
void init_comunication();


//* LED.CPP
void toggle_led(bool s);
void init_led();
void wake_task_blink_led_once(int DELAY = -1);
void resume_loop_blink(int DELAY = -1);
void suspend_loop_blink();
void set_loop_blink_delay(int DELAY);



//* DEBUG_PRINT.CPP
void print_info_uart_struct(InfoUART* info);
const char* get_role_name(int role);
void print_msg_struct(Msg* msg);


//* UART.CPP
void sort_new_msg(Msg *msg);
void task_receive_uart(void *arg);
Msg* create_msg(int sender_id, int target_id, MsgType type, Payload payload);
void send_buffered_messages_to_master();
void send_msg_to_master(Msg* msg);
void send_buffered_messages_to_slave();
void send_msg_to_slave(Msg* msg);
void task_send_uart(void *arg);
void init_uart(uart_port_t uart_num, int rx_pin, int tx_pin);


//* HANDLE_HANDSHAKE.CPP
void task_ping_slave(void* info);
void task_ping_master(void* info);
void task_handle_handshakes(void* info);


//*HANDLE_REPORT.CPP
void task_handle_report(void* arg);
int get_ids_array_len();
void get_ids_array(int arr[]);
void init_report_handler();
void recive_new_report(PayloadReport p);


//* COMMANDS.CPP
void task_execute_command_01(void *arg);
void task_execute_command_02(void *arg);
void task_execute_servo(void *arg);


//*BRIDGE_WIFI.CPP
void convert_servo_instructions(const std::vector<float>& angles);

