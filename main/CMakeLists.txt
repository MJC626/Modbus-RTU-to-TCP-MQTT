idf_component_register(SRCS "modbus_config.c" "main.c" "simple_wifi_sta.c" "uart_rtu.c" "web_server.c" "modbus_task.c" "mqtt.c" "tcp_server.c" "tcp_slave_regs.c"
                    INCLUDE_DIRS "."
                    EMBED_FILES "html/V2.html" "favicon.ico")
