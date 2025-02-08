#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "uart_rtu.h"

// 定义缓冲区大小，使用串口缓冲区的默认大小
#define BUF_SIZE UART_BUFFER_SIZE

// 声明UART0、UART1和UART2的事件队列句柄
static QueueHandle_t uart0_queue;
static QueueHandle_t uart1_queue;
static QueueHandle_t uart2_queue;
// 声明用于接收数据同步的信号量句柄
static SemaphoreHandle_t rx0_sem;
static SemaphoreHandle_t rx1_sem;
static SemaphoreHandle_t rx2_sem;

//UART0发送数据
int send_data0(uint8_t *buf, int len) {
    return uart_write_bytes_with_break(UART_NUM_0, (const char*)buf, len, 100);
}

//UART1发送数据
int send_data1(uint8_t *buf, int len) {
    return uart_write_bytes_with_break(UART_NUM_1, (const char*)buf, len, 100);
}
//UART2发送数据
int send_data2(uint8_t *buf, int len) {
    return uart_write_bytes_with_break(UART_NUM_2, (const char*)buf, len, 100);
}

//UART0接收数据
int receive_data0(uint8_t *buf, int bufsz, int timeout, int bytes_timeout) {
    int len = 0;
    int rc;
    TickType_t start = xTaskGetTickCount();
    
    while (1) {
        if (xSemaphoreTake(rx0_sem, pdMS_TO_TICKS(timeout)) == pdTRUE) {
            rc = uart_read_bytes(UART_NUM_0, buf + len, bufsz, pdMS_TO_TICKS(bytes_timeout));
            if (rc > 0) {
                len += rc;
                bufsz -= rc;
                if (bufsz == 0) break;
            } else if (rc == 0) break;
        } else break;
        
        if ((xTaskGetTickCount() - start) * portTICK_PERIOD_MS >= timeout)
            break;
    }
    return len;
}

//UART1接收数据
int receive_data1(uint8_t *buf, int bufsz, int timeout, int bytes_timeout) {
     // 已接收到的总字节数
    int len = 0;
    // 当前接收到的字节数
    int rc;
    // 记录开始时间
    TickType_t start = xTaskGetTickCount();
    
    while (1) {
        // 尝试获取接收数据的信号量，等待时间由timeout参数决定(信号量超时,确保任务不会死锁)
        if (xSemaphoreTake(rx1_sem, pdMS_TO_TICKS(timeout)) == pdTRUE) {
            // 从UART1中读取数据，读取的字节数受bytes_timeout参数控制
            rc = uart_read_bytes(UART_NUM_1, buf + len, bufsz, pdMS_TO_TICKS(bytes_timeout));
            if (rc > 0) {
                // 累加读取的字节数并减少剩余缓冲区大小
                len += rc;
                bufsz -= rc;
                // 如果缓冲区已满，则退出循环
                if (bufsz == 0) break;
            } else if (rc == 0) break;// 未读取到更多数据，退出循环

        } else break;// 获取信号量失败（例如超时），退出循环
        // 检查是否超过总超时时间
        if ((xTaskGetTickCount() - start) * portTICK_PERIOD_MS >= timeout)
            break;
    }
    return len;// 返回实际接收到的字节数
}
//UART2接收数据
int receive_data2(uint8_t *buf, int bufsz, int timeout, int bytes_timeout) {
    int len = 0;
    int rc;
    TickType_t start = xTaskGetTickCount();
    
    while (1) {
        if (xSemaphoreTake(rx2_sem, pdMS_TO_TICKS(timeout)) == pdTRUE) {
            rc = uart_read_bytes(UART_NUM_2, buf + len, bufsz, pdMS_TO_TICKS(bytes_timeout));
            if (rc > 0) {
                len += rc;
                bufsz -= rc;
                if (bufsz == 0) break;
            } else if (rc == 0) break;
        } else break;
        
        if ((xTaskGetTickCount() - start) * portTICK_PERIOD_MS >= timeout)
            break;
    }
    return len;
}

// UART0事件处理任务函数
static void uart0_event_task(void *pvParameters) {
    uart_event_t event;
    size_t buffered_size;
    
    while (1) {
        if (xQueueReceive(uart0_queue, (void *)&event, (TickType_t)portMAX_DELAY)) {
            switch (event.type) {
                case UART_DATA:
                    uart_get_buffered_data_len(UART_NUM_0, &buffered_size);
                    xSemaphoreGive(rx0_sem);
                    break;
                case UART_FIFO_OVF:
                case UART_BUFFER_FULL:
                    uart_flush_input(UART_NUM_0);
                    xQueueReset(uart0_queue);
                    break;
                default:
                    break;
            }
        }
    }
}

// UART1事件处理任务函数
static void uart1_event_task(void *pvParameters) {
    // 定义用于存储UART事件的结构体
    uart_event_t event;
    // 存储缓冲区中剩余数据长度的变量
    size_t buffered_size;
    
    // 无限循环，用于持续处理UART事件
    while (1) {
        // 从UART1的事件队列中接收事件，等待时间为无限（portMAX_DELAY）
        // 如果成功接收到事件，则处理它
        if (xQueueReceive(uart1_queue, (void *)&event, (TickType_t)portMAX_DELAY)) {
            // 根据接收到的事件类型进行分类处理
            switch (event.type) {
                // 事件类型为UART_DATA：UART接收到数据
                case UART_DATA:
                    // 获取UART缓冲区中存储的剩余数据长度
                    uart_get_buffered_data_len(UART_NUM_1, &buffered_size);
                    // 释放信号量，通知接收任务可以开始读取数据
                    xSemaphoreGive(rx1_sem);
                    break;
                
                // 事件类型为UART_FIFO_OVF：UART硬件FIFO溢出
                case UART_FIFO_OVF:
                // 事件类型为UART_BUFFER_FULL：UART软件缓冲区已满
                case UART_BUFFER_FULL:
                    // 清空UART的输入缓冲区，避免数据残留导致异常
                    uart_flush_input(UART_NUM_1);
                    // 重置UART的事件队列，清除所有未处理的事件
                    xQueueReset(uart1_queue);
                    break;
                
                // 其他类型的事件不做特殊处理
                default:
                    break;
            }
        }
    }
}

static void uart2_event_task(void *pvParameters) {
    uart_event_t event;
    size_t buffered_size;
    
    while (1) {
        if (xQueueReceive(uart2_queue, (void *)&event, (TickType_t)portMAX_DELAY)) {
            switch (event.type) {
                case UART_DATA:
                    uart_get_buffered_data_len(UART_NUM_2, &buffered_size);
                    xSemaphoreGive(rx2_sem);
                    break;
                case UART_FIFO_OVF:
                case UART_BUFFER_FULL:
                    uart_flush_input(UART_NUM_2);
                    xQueueReset(uart2_queue);
                    break;
                default:
                    break;
            }
        }
    }
}

//初始化UART0、UART1和UART2
int uart_init(void) {
    // UART基本配置
    uart_config_t uart_config = {
        .baud_rate = 115200,          // 波特率
        .data_bits = UART_DATA_8_BITS,// 数据位
        .parity = UART_PARITY_DISABLE,// 无校验
        .stop_bits = UART_STOP_BITS_1,// 1位停止位
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,  // 禁用硬件流控
        .source_clk = UART_SCLK_APB,  // 使用APB时钟源
    };
    
    // UART0 配置
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_0, 6, 7, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, BUF_SIZE * 2, BUF_SIZE * 2, 20, &uart0_queue, ESP_INTR_FLAG_IRAM));
    uart_set_rx_timeout(UART_NUM_0, 3); // 设置超时为3个字符时间(硬件超时)
    uart_set_rx_full_threshold(UART_NUM_0, 120);

    // UART1 配置
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, 17, 18, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, BUF_SIZE * 2, BUF_SIZE * 2, 20, &uart1_queue, ESP_INTR_FLAG_IRAM));
    uart_set_rx_timeout(UART_NUM_1, 3); // 设置超时为3个字符时间(硬件超时)
    uart_set_rx_full_threshold(UART_NUM_1, 120);

    // UART2 配置
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_2, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, 15, 16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, BUF_SIZE * 2, BUF_SIZE * 2, 20, &uart2_queue, ESP_INTR_FLAG_IRAM));
    uart_set_rx_timeout(UART_NUM_2, 3); // 设置超时为3个字符时间(硬件超时)
    uart_set_rx_full_threshold(UART_NUM_2, 120);
    
    rx0_sem = xSemaphoreCreateBinary();
    rx1_sem = xSemaphoreCreateBinary();
    rx2_sem = xSemaphoreCreateBinary();
    if (rx0_sem == NULL || rx1_sem == NULL || rx2_sem == NULL) {
        ESP_LOGE("UART", "Failed to create rx semaphores");
        return -1;
    }
    
    xTaskCreate(uart0_event_task, "uart0_event_task", 2048, NULL, 12, NULL);
    xTaskCreate(uart1_event_task, "uart1_event_task", 2048, NULL, 12, NULL);
    xTaskCreate(uart2_event_task, "uart2_event_task", 2048, NULL, 12, NULL);
    
    return ESP_OK;
}