#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modbus_task.h"
#include "modbus_config.h"
#include "uart_rtu.h"
#include "esp_log.h"

// 日志标签
static const char *TAG = "modbus_task";

// 两个 Modbus 主站定义发送和接收缓冲区
static uint8_t master1_send_buf[AGILE_MODBUS_MAX_ADU_LENGTH];
static uint8_t master1_recv_buf[AGILE_MODBUS_MAX_ADU_LENGTH];
static uint8_t master2_send_buf[AGILE_MODBUS_MAX_ADU_LENGTH];
static uint8_t master2_recv_buf[AGILE_MODBUS_MAX_ADU_LENGTH];

// 初始化两个UART的Modbus上下文
static modbus_context_t mb_ctx1 = {0};
static modbus_context_t mb_ctx2 = {0};

void start_modbus(void)
{ // 初始化UART1的Modbus
    agile_modbus_rtu_init(&mb_ctx1.ctx_rtu, master1_send_buf, sizeof(master1_send_buf),
                          master1_recv_buf, sizeof(master1_recv_buf));
    mb_ctx1.uart_port = 1;

    // 初始化UART2的Modbus
    agile_modbus_rtu_init(&mb_ctx2.ctx_rtu, master2_send_buf, sizeof(master2_send_buf),
                          master2_recv_buf, sizeof(master2_recv_buf));
    mb_ctx2.uart_port = 2;

    // 创建两个Modbus任务
    xTaskCreate(modbus_poll_task,
                "modbus_task1",
                MODBUS_TASK_STACK_SIZE,
                &mb_ctx1,
                5,
                NULL);

    xTaskCreate(modbus_poll_task,
                "modbus_task2",
                MODBUS_TASK_STACK_SIZE,
                &mb_ctx2,
                5,
                NULL);
}

void modbus_poll_task(void *pvParameters)
{
    // 获取传入的 Modbus 上下文指针
    modbus_context_t *mb_ctx = (modbus_context_t *)pvParameters;
    // 获取 Modbus RTU 上下文
    agile_modbus_t *ctx = &mb_ctx->ctx_rtu._ctx;
    uint8_t target_uart = mb_ctx->uart_port; // 直接获取目标 UART 端口

    while (1)
    {
        // 遍历所有的 Modbus 组
        for (int i = 0; i < modbus_config.group_count; i++)
        {
        // 直接跳过不匹配的 UART 组
        if (!modbus_config.groups[i].enabled || modbus_config.groups[i].uart_port != target_uart) {
            continue;
        }

            // 设置当前组的从设备地址
            agile_modbus_set_slave(ctx, modbus_config.groups[i].slave_addr);

            int send_len = 0;
            // 根据功能码选择合适的 Modbus 请求序列化方式
            switch (modbus_config.groups[i].function_code)
            {
            case 1: // Read Coils
                send_len = agile_modbus_serialize_read_bits(ctx,
                                                            modbus_config.groups[i].start_addr,
                                                            modbus_config.groups[i].reg_count);
                break;
            case 2: // Read Discrete Inputs
                send_len = agile_modbus_serialize_read_input_bits(ctx,
                                                                  modbus_config.groups[i].start_addr,
                                                                  modbus_config.groups[i].reg_count);
                break;
            case 3: // Read Holding Registers
                send_len = agile_modbus_serialize_read_registers(ctx,
                                                                 modbus_config.groups[i].start_addr,
                                                                 modbus_config.groups[i].reg_count);
                break;
            case 4: // Read Input Registers
                send_len = agile_modbus_serialize_read_input_registers(ctx,
                                                                       modbus_config.groups[i].start_addr,
                                                                       modbus_config.groups[i].reg_count);
                break;
            default:
                ESP_LOGE(TAG, "UART%d 组 %d 不支持的功能码: %d",
                         mb_ctx->uart_port, i, modbus_config.groups[i].function_code);
                continue;
            }

            // 如果请求序列化成功
            if (send_len > 0)
            {
                int read_len;
                // 根据 UART 端口选择不同的发送和接收函数
                if (mb_ctx->uart_port == 1)
                {
                    send_data1(ctx->send_buf, send_len);
                    read_len = receive_data1(ctx->read_buf, ctx->read_bufsz, 1000, 20);
                }
                else
                {
                    send_data2(ctx->send_buf, send_len);
                    read_len = receive_data2(ctx->read_buf, ctx->read_bufsz, 1000, 20);
                }

                if (read_len > 0)
                {
                    int rc = -1;

                    // 根据功能码选择合适的反序列化方式并存储到对应区域
                    switch (modbus_config.groups[i].function_code)
                    {
                    case 1:
                    {   // Read Coils
                        /* 每个元素存储一个位，改为足够大的数组 */
                        uint8_t bit_values[MAX_BITS] = {0};
                        rc = agile_modbus_deserialize_read_bits(ctx, read_len, bit_values);
                        if (rc >= 0)
                        {
                            /* 确保不超过寄存器容量和数组边界 */
                            int max_bits = sizeof(modbus_data.coils[i]) * 8;
                            int num_bits = rc < max_bits ? rc : max_bits;

                            /* 逐个位存储到对应字节的对应bit位 */
                            for (int j = 0; j < num_bits; j++)
                            {
                                int byte_idx = j / 8;
                                int bit_idx = j % 8;
                                if (bit_values[j])
                                {
                                    modbus_data.coils[i][byte_idx] |= (1 << bit_idx);
                                }
                                else
                                {
                                    modbus_data.coils[i][byte_idx] &= ~(1 << bit_idx);
                                }
                            }
                        }
                        break;
                    }
                    case 2:
                    { // Read Discrete Inputs
                        uint8_t bit_values[MAX_BITS] = {0};
                        rc = agile_modbus_deserialize_read_input_bits(ctx, read_len, bit_values);
                        if (rc >= 0)
                        {
                            int max_bits = sizeof(modbus_data.discrete_inputs[i]) * 8;
                            int num_bits = rc < max_bits ? rc : max_bits;

                            for (int j = 0; j < num_bits; j++)
                            {
                                int byte_idx = j / 8;
                                int bit_idx = j % 8;
                                if (bit_values[j])
                                {
                                    modbus_data.discrete_inputs[i][byte_idx] |= (1 << bit_idx);
                                }
                                else
                                {
                                    modbus_data.discrete_inputs[i][byte_idx] &= ~(1 << bit_idx);
                                }
                            }
                        }
                        break;
                    }
                    case 3:
                    { // Read Holding Registers
                        uint16_t reg_values[MAX_REGS] = {0};
                        rc = agile_modbus_deserialize_read_registers(ctx, read_len, reg_values);
                        if (rc >= 0)
                        {
                            memcpy(modbus_data.holding_regs[i], reg_values,
                                   sizeof(uint16_t) * modbus_config.groups[i].reg_count);
                        }
                        break;
                    }
                    case 4:
                    { // Read Input Registers
                        uint16_t reg_values[MAX_REGS] = {0};
                        rc = agile_modbus_deserialize_read_input_registers(ctx, read_len, reg_values);
                        if (rc >= 0)
                        {
                            memcpy(modbus_data.input_regs[i], reg_values,
                                   sizeof(uint16_t) * modbus_config.groups[i].reg_count);
                        }
                        break;
                    }
                    }

                    if (rc >= 0)
                    {
                        modbus_data.register_ready[i] = true;
                        ESP_LOGI(TAG, "UART%d 组 %d FC%d 数据采集成功",
                                 mb_ctx->uart_port, i, modbus_config.groups[i].function_code);
                    }
                    else
                    {
                        ESP_LOGE(TAG, "UART%d 组 %d FC%d 数据解析失败",
                                 mb_ctx->uart_port, i, modbus_config.groups[i].function_code);
                        modbus_data.register_ready[i] = false;
                    }
                }
                else
                {
                    ESP_LOGE(TAG, "UART%d 组 %d FC%d 读取超时",
                             mb_ctx->uart_port, i, modbus_config.groups[i].function_code);
                    modbus_data.register_ready[i] = false;
                }
            }
            else
            {
                ESP_LOGE(TAG, "UART%d 组 %d FC%d 请求打包失败",
                         mb_ctx->uart_port, i, modbus_config.groups[i].function_code);
                modbus_data.register_ready[i] = false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(modbus_config.poll_interval));
    }
}