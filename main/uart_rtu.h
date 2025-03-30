#ifndef __uart_rtu_H
#define __uart_rtu_H

#include "driver/uart.h"

#define UART_BUFFER_SIZE    256 //定义串口缓冲区大小

// 波特率枚举
typedef enum {
    BAUD_9600 = 9600,
    BAUD_19200 = 19200,
    BAUD_38400 = 38400,
    BAUD_57600 = 57600,
    BAUD_115200 = 115200
} uart_rtu_baud_rate_t;

// 数据位枚举
typedef enum {
    DATA_BITS_5 = UART_DATA_5_BITS,
    DATA_BITS_6 = UART_DATA_6_BITS,
    DATA_BITS_7 = UART_DATA_7_BITS,
    DATA_BITS_8 = UART_DATA_8_BITS
} uart_rtu_data_bits_t;

// 校验位枚举
typedef enum {
    PARITY_NONE = UART_PARITY_DISABLE,
    PARITY_EVEN = UART_PARITY_EVEN,
    PARITY_ODD = UART_PARITY_ODD
} uart_rtu_parity_t;

// 停止位枚举
typedef enum {
    STOP_BITS_1 = UART_STOP_BITS_1,
    STOP_BITS_1_5 = UART_STOP_BITS_1_5,
    STOP_BITS_2 = UART_STOP_BITS_2
} uart_rtu_stop_bits_t;

// 定义UART参数结构体
typedef struct {
    uart_rtu_baud_rate_t baud_rate;
    uart_rtu_data_bits_t data_bits;
    uart_rtu_parity_t parity;
    uart_rtu_stop_bits_t stop_bits;
} uart_param_t;

extern uart_param_t uart_params[3];

int send_data0(uint8_t *buf, int len);
int send_data1(uint8_t *buf, int len);
int send_data2(uint8_t *buf, int len);
int receive_data0(uint8_t *buf, int bufsz, int timeout);
int receive_data1(uint8_t *buf, int bufsz, int timeout);
int receive_data2(uint8_t *buf, int bufsz, int timeout);
int uart_init(void);

// 从NVS中读取UART参数
esp_err_t load_uart_params_from_nvs(void);
// 保存UART参数到NVS
esp_err_t save_uart_params_to_nvs(void);

#endif
