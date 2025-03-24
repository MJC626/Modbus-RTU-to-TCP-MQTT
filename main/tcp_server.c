#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "tcp_server.h"
#include "agile_modbus.h"
#include "agile_modbus_slave_util.h"
#include "tcp_slave_regs.h"

static const char *TAG = "modbus_tcp_slave";

// 地址检查回调函数
static int addr_check(agile_modbus_t *ctx, struct agile_modbus_slave_info *slave_info) {
    int slave = slave_info->sft->slave;
    if ((slave != ctx->slave) && (slave != AGILE_MODBUS_BROADCAST_ADDRESS))
    return -AGILE_MODBUS_EXCEPTION_UNKNOW;
    int function = slave_info->sft->function;
    int address = slave_info->address;
    int nb = slave_info->nb;
    
    // 根据功能码检查不同类型的寄存器
    switch (function) {
        case AGILE_MODBUS_FC_READ_COILS:
        case AGILE_MODBUS_FC_WRITE_SINGLE_COIL:
        case AGILE_MODBUS_FC_WRITE_MULTIPLE_COILS:
            if (address < 0 || address + nb > tcp_slave.reg_sizes.tab_bits_size) {
                ESP_LOGE("MODBUS", "Invalid coil address: start=%d, count=%d, max=%d", 
                         address, nb, tcp_slave.reg_sizes.tab_bits_size);
                return -AGILE_MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
            }
            break;
            
        case AGILE_MODBUS_FC_READ_DISCRETE_INPUTS:
            if (address < 0 || address + nb > tcp_slave.reg_sizes.tab_input_bits_size) {
                ESP_LOGE("MODBUS", "Invalid discrete input address: start=%d, count=%d, max=%d", 
                         address, nb, tcp_slave.reg_sizes.tab_input_bits_size);
                return -AGILE_MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
            }
            break;
            
        case AGILE_MODBUS_FC_READ_HOLDING_REGISTERS:
        case AGILE_MODBUS_FC_WRITE_SINGLE_REGISTER:
        case AGILE_MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
        case AGILE_MODBUS_FC_MASK_WRITE_REGISTER:
            if (address < 0 || address + nb > tcp_slave.reg_sizes.tab_registers_size) {
                ESP_LOGE("MODBUS", "Invalid holding register address: start=%d, count=%d, max=%d", 
                         address, nb, tcp_slave.reg_sizes.tab_registers_size);
                return -AGILE_MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
            }
            break;
            
        case AGILE_MODBUS_FC_READ_INPUT_REGISTERS:
            if (address < 0 || address + nb > tcp_slave.reg_sizes.tab_input_registers_size) {
                ESP_LOGE("MODBUS", "Invalid input register address: start=%d, count=%d, max=%d", 
                         address, nb, tcp_slave.reg_sizes.tab_input_registers_size);
                return -AGILE_MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
            }
            break;
            
        case AGILE_MODBUS_FC_WRITE_AND_READ_REGISTERS: {
            // 对于写读组合功能，需要单独处理
            int address_read = address;
            int nb_read = nb;
            int address_write = (slave_info->buf[2] << 8) + slave_info->buf[3];
            int nb_write = (slave_info->buf[4] << 8) + slave_info->buf[5];
            
            // 检查读取地址范围
            if (address_read < 0 || address_read + nb_read > tcp_slave.reg_sizes.tab_registers_size) {
                ESP_LOGE("MODBUS", "Invalid read register address: start=%d, count=%d, max=%d", 
                         address_read, nb_read, tcp_slave.reg_sizes.tab_registers_size);
                return -AGILE_MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
            }
            
            // 检查写入地址范围
            if (address_write < 0 || address_write + nb_write > tcp_slave.reg_sizes.tab_registers_size) {
                ESP_LOGE("MODBUS", "Invalid write register address: start=%d, count=%d, max=%d", 
                         address_write, nb_write, tcp_slave.reg_sizes.tab_registers_size);
                return -AGILE_MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
            }
            break;
        }
    }
    
    return 0;
}


// Modbus 从机配置结构体
const agile_modbus_slave_util_t slave_util = {
    bit_maps,                    // 线圈映射
    1,                          // 线圈映射数量
    input_bit_maps,             // 离散输入映射
    1,                          // 离散输入映射数量
    register_maps,              // 保持寄存器映射
    1,                          // 保持寄存器映射数量
    input_register_maps,        // 输入寄存器映射
    1,                          // 输入寄存器映射数量
    addr_check,                 // 需要地址检查
    NULL,                       // 不需要预处理
    NULL                        // 不需要后处理
};

typedef struct {
    int socket;
    TaskHandle_t task;
} client_info_t;

// 全局变量声明
static client_info_t clients[MAX_CLIENTS] = {0};
static SemaphoreHandle_t clients_mutex = NULL;

static void handle_client(void *pvParameters)
{
    int sock = (int)pvParameters;
    uint8_t ctx_send_buf[AGILE_MODBUS_MAX_ADU_LENGTH];
    uint8_t ctx_read_buf[AGILE_MODBUS_MAX_ADU_LENGTH];
    
    // 初始化 Modbus TCP 上下文
    agile_modbus_tcp_t ctx_tcp;
    agile_modbus_t *ctx = &ctx_tcp._ctx;
    agile_modbus_tcp_init(&ctx_tcp, ctx_send_buf, sizeof(ctx_send_buf), 
                         ctx_read_buf, sizeof(ctx_read_buf));
    agile_modbus_set_slave(ctx, tcp_slave.slave_address);

    // 设置非阻塞模式
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    while (1) {
        fd_set readfds;
        struct timeval timeout;
        
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        
        timeout.tv_sec = 1;  // 1秒超时
        timeout.tv_usec = 0;

        int activity = select(sock + 1, &readfds, NULL, NULL, &timeout);
        
        if (activity < 0) {
            ESP_LOGE(TAG, "Select error: errno %d", errno);
            break;
        }
        
        if (activity == 0) {
            // 超时，继续等待
            continue;
        }

        if (FD_ISSET(sock, &readfds)) {
            // 接收 Modbus 请求
            int rc = recv(sock, ctx->read_buf, ctx->read_bufsz, 0);
            if (rc <= 0) {
                if (rc < 0 && errno != EAGAIN) {
                    ESP_LOGE(TAG, "Recv error: errno %d", errno);
                }
                break;
            }

            // 处理 Modbus 请求
            int send_len = agile_modbus_slave_handle(ctx, rc, 0, 
                                                   agile_modbus_slave_util_callback,
                                                   &slave_util, NULL);
            
            // 发送响应
            if (send_len > 0) {
                int remaining = send_len;
                uint8_t *ptr = ctx->send_buf;
                
                while (remaining > 0) {
                    int written = send(sock, ptr, remaining, 0);
                    if (written < 0) {
                        if (errno != EAGAIN) {
                            ESP_LOGE(TAG, "Send error: errno %d", errno);
                            goto cleanup;
                        }
                        // 如果是 EAGAIN，等待一下再重试
                        vTaskDelay(pdMS_TO_TICKS(10));
                        continue;
                    }
                    remaining -= written;
                    ptr += written;
                }
            }
        }
    }

cleanup:
    // 清理客户端连接
    if (clients_mutex != NULL) {
        xSemaphoreTake(clients_mutex, portMAX_DELAY);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].socket == sock) {
                ESP_LOGI(TAG, "Cleaning up client slot %d", i);
                shutdown(clients[i].socket, 0);
                close(clients[i].socket);
                clients[i].socket = 0;
                clients[i].task = NULL;
                break;
            }
        }
        xSemaphoreGive(clients_mutex);
    }
    
    ESP_LOGI(TAG, "Client connection closed, deleting task");
    vTaskDelete(NULL);  // 删除当前任务
}

static void tcp_server_task(void *pvParameters)
{
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(tcp_slave.server_port);

    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Socket creation failed: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    ESP_LOGI(TAG, "Socket created");

    if (bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
        ESP_LOGE(TAG, "Socket bind failed: errno %d", errno);
        goto cleanup;
    }

    if (listen(listen_sock, MAX_CLIENTS) != 0) {
        ESP_LOGE(TAG, "Socket listen failed: errno %d", errno);
        goto cleanup;
    }

    // 创建互斥锁
    clients_mutex = xSemaphoreCreateMutex();
    if (clients_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        goto cleanup;
    }

    while (1) {
        ESP_LOGI(TAG, "Modbus TCP slave listening on port %d", tcp_slave.server_port);

        struct sockaddr_in source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Accept failed: errno %d", errno);
            break;
        }

        // 设置 keepalive
        int keepalive = 1;
        int keepidle = SERVER_KEEPALIVE_IDLE;
        int keepintvl = SERVER_KEEPALIVE_INTERVAL;
        int keepcnt = SERVER_KEEPALIVE_COUNT;

        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(int));

        char addr_str[128];
        inet_ntoa_r(source_addr.sin_addr, addr_str, sizeof(addr_str) - 1);
        
        // 查找空闲的客户端槽位
        xSemaphoreTake(clients_mutex, portMAX_DELAY);
        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].socket == 0) {
                slot = i;
                break;
            }
        }
        
        if (slot == -1) {
            ESP_LOGW(TAG, "Maximum clients reached, rejecting connection from %s", addr_str);
            xSemaphoreGive(clients_mutex);
            shutdown(sock, 0);
            close(sock);
            continue;
        }

        ESP_LOGI(TAG, "Client connected on slot %d: %s", slot, addr_str);
        clients[slot].socket = sock;
        
        // 为每个客户端创建一个任务
        char task_name[32];
        snprintf(task_name, sizeof(task_name), "client_%d", slot);
        if (xTaskCreate(handle_client, task_name, SERVER_TASK_STACK_SIZE, 
                       (void*)(intptr_t)sock, SERVER_TASK_PRIORITY, &clients[slot].task) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create client task");
            clients[slot].socket = 0;
            shutdown(sock, 0);
            close(sock);
        }
        
        xSemaphoreGive(clients_mutex);
    }

cleanup:
    if (clients_mutex != NULL) {
        vSemaphoreDelete(clients_mutex);
        clients_mutex = NULL;
    }
    close(listen_sock);
    vTaskDelete(NULL);       
}

void start_tcp_server(void)
{
    // 初始化客户端数组
    memset(clients, 0, sizeof(clients));
    
    // 初始化 Modbus tcp寄存器
    init_tcp_slave_regs();
    
    // 创建 TCP 服务器任务
    xTaskCreate(tcp_server_task, "modbus_tcp_slave", SERVER_TASK_STACK_SIZE, 
                NULL, SERVER_TASK_PRIORITY, NULL);

    xTaskCreate(modbus_regs_update_task, "modbus_update", 4096, NULL, 5, NULL);
}