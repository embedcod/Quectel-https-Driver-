/*
 * Quectel.h
 *
 *  Created on: Dec 11, 2024
 *      Author: ERIC MULWA
 */

#ifndef QUECTEL_H
#define QUECTEL_H

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

// Definitions
#define MODEM_UART_NUM UART_NUM_2
#define TX_PIN 26
#define RX_PIN 25
#define RESET_PIN 27
#define PWR_PIN 13 
#define ESP_BAUDRATE 115200
#define QUECTEL_BAUDRATE 115200
#define TAG_GSM "MODEM"
#define BUF_SIZE 512

static const int RETRIES = 4;
static const int CMD_DELAY_MS = 4000;

// Declare external payload function
extern char* create_json_payload();
extern uint32_t getChipId();
extern const char *getDeviceUrl(uint32_t chipId);
extern const char *getHttpPostHeader(uint32_t chipId);
extern char APN[32];
extern void soundBuzzertwo(int duration_ms, int delay_between_ms);

void uart_init();
void gsm_reset();
void gsm_poweron();
void hardware_poweroff();
void at_poweroff();
bool send_at_command(const char *command, const char *expected_response, int retries, uint32_t timeout_ms);
void gsm_comm_task();

#endif // QUECTEL_H
