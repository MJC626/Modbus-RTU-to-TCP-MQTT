#ifndef __uart_rtu_H
#define __uart_rtu_H

#include "driver/uart.h"

#define UART_BUFFER_SIZE    256 //定义串口缓冲区大小

int send_data0(uint8_t *buf, int len);
int send_data1(uint8_t *buf, int len);
int send_data2(uint8_t *buf, int len);
int receive_data0(uint8_t *buf, int bufsz, int timeout, int bytes_timeout);
int receive_data1(uint8_t *buf, int bufsz, int timeout, int bytes_timeout);
int receive_data2(uint8_t *buf, int bufsz, int timeout, int bytes_timeout);
int uart_init(void);

#endif
