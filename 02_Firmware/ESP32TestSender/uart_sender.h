#pragma once

#include <Arduino.h>

void uart_sender_init();
bool uart_sender_set_baudrate(uint32_t baudrate);
uint32_t uart_sender_get_baudrate();
bool uart_sender_is_ready();
size_t uart_sender_write(const uint8_t* data, size_t len);
void uart_sender_flush();
