#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "rpc.pb.h"
#include "pb_encode.h"
#include "pb_decode.h"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 12345
#define BUFFER_SIZE 256
#define CONNECTION_POOL_SIZE 5

// 连接池结构体
typedef struct {
    int connections[CONNECTION_POOL_SIZE];
    int available[CONNECTION_POOL_SIZE];
    pthread_mutex_t lock;
} ConnectionPool;

static void print_hex(unsigned char *in, int in_len)
{
    int i;
    for(i=0;i<in_len;i++)
    {
        printf("%02x", in[i]);
    }
    printf("\n");
}

bool encode_string_callback(pb_ostream_t *stream, const pb_field_t *field, void *const *arg) {
    const char *str = (const char *)(*arg);

    if (!pb_encode_tag_for_field(stream, field))
        return false;
    
    return pb_encode_string(stream, (const pb_byte_t *)str, strlen(str));
}

// 序列化 RPC 请求
size_t encode_request(RPCRequest *request, uint8_t *buffer, size_t buf_size) {
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, buf_size);
    if (pb_encode(&stream, RPCRequest_fields, request)) {
        return stream.bytes_written;
    }
    return 0;
}

// 解析 RPC 响应
bool decode_response(uint8_t *buffer, size_t len, RPCResponse *response) {
    pb_istream_t stream = pb_istream_from_buffer(buffer, len);
    return pb_decode(&stream, RPCResponse_fields, response);
}

// 初始化连接池
void init_connection_pool(ConnectionPool *pool) {
    pthread_mutex_init(&pool->lock, NULL);
    for (int i = 0; i < CONNECTION_POOL_SIZE; i++) {
        pool->connections[i] = socket(AF_INET, SOCK_STREAM, 0);
        if (pool->connections[i] == -1) {
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(SERVER_PORT);
        inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);
        if (connect(pool->connections[i], (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
            perror("Connect failed");
            close(pool->connections[i]);
            exit(EXIT_FAILURE);
        }
        pool->available[i] = 1;
    }
}

// 获取一个可用的连接
int get_connection(ConnectionPool *pool) {
    pthread_mutex_lock(&pool->lock);
    for (int i = 0; i < CONNECTION_POOL_SIZE; i++) {
        if (pool->available[i]) {
            pool->available[i] = 0;
            pthread_mutex_unlock(&pool->lock);
            return pool->connections[i];
        }
    }
    pthread_mutex_unlock(&pool->lock);
    return -1;
}

// 释放连接
void release_connection(ConnectionPool *pool, int conn) {
    pthread_mutex_lock(&pool->lock);
    for (int i = 0; i < CONNECTION_POOL_SIZE; i++) {
        if (pool->connections[i] == conn) {
            pool->available[i] = 1;
            break;
        }
    }
    pthread_mutex_unlock(&pool->lock);
}

// 重新建立连接
int reestablish_connection() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("Socket creation failed");
        return -1;
    }
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Connect failed");
        close(sockfd);
        return -1;
    }
    return sockfd;
}

// 处理连接关闭
void handle_connection_closed(ConnectionPool *pool, int conn) {
    pthread_mutex_lock(&pool->lock);
    for (int i = 0; i < CONNECTION_POOL_SIZE; i++) {
        if (pool->connections[i] == conn) {
            close(pool->connections[i]);
            int new_conn = reestablish_connection();
            if (new_conn != -1) {
                pool->connections[i] = new_conn;
            }
            pool->available[i] = 1;
            break;
        }
    }
    pthread_mutex_unlock(&pool->lock);
}

// 发送 RPC 请求
void send_rpc_request(ConnectionPool *pool, const char *method, int num1, int num2) {
    int sockfd = get_connection(pool);
    if (sockfd == -1) {
        printf("No available connections in the pool.\n");
        return;
    }

    uint8_t buffer[BUFFER_SIZE];
    RPCRequest request = RPCRequest_init_zero;
    request.method.funcs.encode = &encode_string_callback;
    request.method.arg = (void *)method;
    request.has_num1 = true;
    request.num1 = num1;
    request.has_num2 = true;
    request.num2 = num2;

    size_t req_len = encode_request(&request, buffer, BUFFER_SIZE);
    printf("send:");
    print_hex(buffer, req_len);

    int retries = 1; // 重试次数
    while (retries > 0) {
        if (send(sockfd, buffer, req_len, 0) == -1) {
            perror("Send failed");
            handle_connection_closed(pool, sockfd);
            sockfd = get_connection(pool);
            if (sockfd == -1) {
                printf("No available connections in the pool after reconnection.\n");
                return;
            }
            retries--;
            continue;
        }

        int resp_len = recv(sockfd, buffer, BUFFER_SIZE, 0);
        if (resp_len <= 0) {
            if (resp_len == 0) {
                printf("Server closed the connection.\n");
            } else {
                perror("Error receiving data from server");
            }
            handle_connection_closed(pool, sockfd);
            sockfd = get_connection(pool);
            if (sockfd == -1) {
                printf("No available connections in the pool after reconnection.\n");
                return;
            }
            retries--;
            continue;
        }

        RPCResponse response = RPCResponse_init_zero;
        if (decode_response(buffer, resp_len, &response)) {
            if (response.which_result == RPCResponse_value_tag) {
                printf("Result: %d\n", response.result.value);
            } else {
                char error_msg[BUFFER_SIZE] = {0};
                pb_istream_t err_stream = pb_istream_from_buffer((uint8_t *)response.result.error.arg, BUFFER_SIZE);
                pb_read(&err_stream, error_msg, BUFFER_SIZE);
                printf("Error: %s\n", error_msg);
            }
        } else {
            printf("Failed to decode response\n");
        }
        break;
    }

    release_connection(pool, sockfd);
}

typedef struct {
    ConnectionPool *pool;
    const char *method;
    int num1;
    int num2;
} RequestParams;


void* thread_send_rpc_request(void* arg) {
    RequestParams *params = (RequestParams*)arg;
    send_rpc_request(params->pool, params->method, params->num1, params->num2);
    free(params);
    return NULL;
}

// 主函数
int main() {
    ConnectionPool pool;
    init_connection_pool(&pool);

    pthread_t threads[5];
    const char *methods[] = {"add", "subtract", "multiply", "divide", "divide"};
    int nums1[] = {10, 10, 10, 10, 10};
    int nums2[] = {5, 5, 5, 5, 0};

    for (int i = 0; i < 5; i++) {
        RequestParams *params = (RequestParams*)malloc(sizeof(RequestParams));
        if (params == NULL) {
            perror("malloc error");
            continue;
        }
        params->pool = &pool;
        params->method = methods[i];
        params->num1 = nums1[i];
        params->num2 = nums2[i];

        if (pthread_create(&threads[i], NULL, thread_send_rpc_request, (void*)params) != 0) {
            perror("pthread_create error");
            free(params);
        }
    }

    // 等待所有线程完成
    for (int i = 0; i < 5; i++) {
        pthread_join(threads[i], NULL);
    }

    // 关闭连接池中的所有连接
    for (int i = 0; i < CONNECTION_POOL_SIZE; i++) {
        close(pool.connections[i]);
    }
    pthread_mutex_destroy(&pool.lock);

    return 0;
}
