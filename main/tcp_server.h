// tcp_server.h
#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#define SERVER_KEEPALIVE_IDLE 5
#define SERVER_KEEPALIVE_INTERVAL 5
#define SERVER_KEEPALIVE_COUNT 3
#define SERVER_TASK_STACK_SIZE 4096
#define SERVER_TASK_PRIORITY 5

void start_tcp_server(void);

#endif
