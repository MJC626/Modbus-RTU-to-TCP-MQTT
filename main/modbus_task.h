#ifndef MODBUS_TASK_H
#define MODBUS_TASK_H

#include "agile_modbus.h"
#include "agile_modbus_rtu.h"

#define MODBUS_TASK_STACK_SIZE 4096

typedef struct {
    agile_modbus_rtu_t ctx_rtu;
    uint8_t uart_port;
} modbus_context_t;

void start_modbus(void);
void modbus_poll_task(void *pvParameters);

#endif