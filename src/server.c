#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <uthash.h>
#include <pthread.h>
#include "rpc.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "thpool.h"

#define PORT 12345
#define MAX_EVENTS 10
#define BUFFER_SIZE 256
#define THREAD_POOL_SIZE 4

typedef int (*rpc_func_t)(int, int);

typedef struct {
    char name[32];
    rpc_func_t func;
    UT_hash_handle hh;
} rpc_entry_t;

pthread_rwlock_t rpc_lock = PTHREAD_RWLOCK_INITIALIZER;
rpc_entry_t *rpc_methods = NULL;
threadpool thpool;

// RPC 方法实现
int rpc_add(int a, int b) { return a + b; }
int rpc_subtract(int a, int b) { return a - b; }
int rpc_multiply(int a, int b) { return a * b; }
int rpc_divide(int a, int b) { return (b == 0) ? 0 : (a / b); }

static void print_hex(unsigned char *in, int in_len)
{
    int i;
    for(i=0;i<in_len;i++)
    {
        printf("%02x", in[i]);
    }
    printf("\n");
}

bool decode_string_callback(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    uint8_t buffer[BUFFER_SIZE] = {0};
    
    /* We could read block-by-block to avoid the large buffer... */
    if (stream->bytes_left > sizeof(buffer) - 1)
        return false;
    
    if (!pb_read(stream, *arg, stream->bytes_left))
        return false;
    
    return true;
}

// 注册方法
void register_rpc_method(const char *name, rpc_func_t func) {
    pthread_rwlock_wrlock(&rpc_lock);
    rpc_entry_t *entry = (rpc_entry_t*)malloc(sizeof(rpc_entry_t));
    if(entry == NULL) {
        perror("malloc error");
        pthread_rwlock_unlock(&rpc_lock);
        return;
    }
    strcpy(entry->name, name);
    entry->func = func;
    HASH_ADD_STR(rpc_methods, name, entry);
    pthread_rwlock_unlock(&rpc_lock);
}

// 解析 RPC 请求
bool decode_request(uint8_t *buffer, size_t len, RPCRequest *request) {
    pb_istream_t stream = pb_istream_from_buffer(buffer, len);
    if (len > BUFFER_SIZE) {
        return false; // 防止缓冲区溢出
    }
    return pb_decode(&stream, RPCRequest_fields, request);
}

// 序列化 RPC 响应
size_t encode_response(RPCResponse *response, uint8_t *buffer, size_t buf_size) {
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, buf_size);
    if (pb_encode(&stream, RPCResponse_fields, response)) {
        return stream.bytes_written;
    }
    return 0;
}

// 处理 RPC 请求
void handle_rpc_request(int client_fd) {
    uint8_t buffer[BUFFER_SIZE];
    int msg_len = recv(client_fd, buffer, BUFFER_SIZE, 0);
    if (msg_len <= 0) {
        close(client_fd);
        return;
    }

    printf("thread %u received:", (unsigned int)pthread_self());
    print_hex(buffer, msg_len);

    RPCRequest request = RPCRequest_init_zero;
    char buf[BUFFER_SIZE] = {0};
    request.method.funcs.decode = &decode_string_callback;
    request.method.arg = buf;
    if (!decode_request(buffer, msg_len, &request)) {
        RPCResponse response = RPCResponse_init_zero;
        size_t resp_len = encode_response(&response, buffer, BUFFER_SIZE);
        send(client_fd, buffer, resp_len, 0);
        close(client_fd);
        return;
    }

    RPCResponse response = RPCResponse_init_zero;

    if(!request.method.arg) {
        printf("request.method.arg = null\n");
        close(client_fd);
        return;
    }

    if(!rpc_methods) {
        printf("pc_methods = null\n");
        close(client_fd);
        return;
    }

    pthread_rwlock_rdlock(&rpc_lock);
    rpc_entry_t *entry;
    HASH_FIND_STR(rpc_methods, request.method.arg, entry);
    pthread_rwlock_unlock(&rpc_lock);

    if (entry && request.has_num1 && request.has_num2) {
        response.which_result = RPCResponse_value_tag;
        response.result.value = entry->func(request.num1, request.num2);
    } else {
        response.which_result = RPCResponse_error_tag;
        response.result.error.funcs.encode = NULL;
        response.result.error.arg = NULL;
    }

    size_t resp_len = encode_response(&response, buffer, BUFFER_SIZE);
    if(send(client_fd, buffer, resp_len, 0) == -1) {
        perror("send error");
    }
    close(client_fd);
}

// 线程池任务
void process_client(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);
    handle_rpc_request(client_fd);
}

int main() {
    int server_fd, client_fd, epoll_fd, event_count;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    struct epoll_event ev, events[MAX_EVENTS];

    register_rpc_method("add", rpc_add);
    register_rpc_method("subtract", rpc_subtract);
    register_rpc_method("multiply", rpc_multiply);
    register_rpc_method("divide", rpc_divide);

    thpool = thpool_init(THREAD_POOL_SIZE);
    if(thpool == NULL) {
        perror("thpool_init error");
        exit(EXIT_FAILURE);
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd < 0) {
        perror("Socket creation failed");
        thpool_destroy(thpool);
        exit(EXIT_FAILURE);
    }
    if(fcntl(server_fd, F_SETFL, O_NONBLOCK) < 0) {
        perror("fcntl failed");
        close(server_fd);
        thpool_destroy(thpool);
        exit(EXIT_FAILURE);
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        thpool_destroy(thpool);
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, SOMAXCONN) < 0) {
        perror("Listen failed");
        close(server_fd);
        thpool_destroy(thpool);
        exit(EXIT_FAILURE);
    }

    epoll_fd = epoll_create1(0);
    if(epoll_fd < 0) {
        perror("Epoll creation failed");
        close(server_fd);
        thpool_destroy(thpool);
        exit(EXIT_FAILURE);
    }
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
        perror("Epoll control failed");
        close(server_fd);
        close(epoll_fd);
        thpool_destroy(thpool);
        exit(EXIT_FAILURE);
    }
    printf("RPC Server started on port %d\n", PORT);
    while (1) {
        event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (event_count < 0) {
            perror("Epoll wait failed");
            if(errno != EINTR) {
                close(server_fd);
                close(epoll_fd);
                thpool_destroy(thpool);
                exit(EXIT_FAILURE);
            }
            continue;
        }
        for (int i = 0; i < event_count; i++) {
            if (events[i].data.fd == server_fd) {
                while ((client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len)) > 0) {
                    if(fcntl(client_fd, F_SETFL, O_NONBLOCK) < 0) {
                        perror("Set client socket to non-blocking mode failed");
                        close(client_fd);
                        continue;
                    }
                    ev.events = EPOLLIN | EPOLLET;
                    ev.data.fd = client_fd;
                    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
                        perror("Epoll control failed");
                        close(client_fd);
                    }
                }
            } else {
                int *client_fd_ptr = malloc(sizeof(int));
                if(client_fd_ptr == NULL) {
                    perror("malloc error");
                    close(events[i].data.fd);
                    continue;
                }
                *client_fd_ptr = events[i].data.fd;
                //printf("thpool_add_work\n");
                if(thpool_add_work(thpool, process_client, client_fd_ptr) == -1) {
                    perror("thpool_add_work error");
                    free(client_fd_ptr);
                    close(events[i].data.fd);
                }
            }
        }
    }
    close(server_fd);
    close(epoll_fd);
    thpool_destroy(thpool);
    return 0;
}
