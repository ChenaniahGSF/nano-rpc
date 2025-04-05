#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "rpc.pb.h"
#include "pb_encode.h"
#include "pb_decode.h"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 12345
#define BUFFER_SIZE 256

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

// 发送 RPC 请求
void send_rpc_request(const char *method, int num1, int num2) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connect failed");
        close(sockfd);
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
    send(sockfd, buffer, req_len, 0);

    int resp_len = recv(sockfd, buffer, BUFFER_SIZE, 0);
    if (resp_len > 0) {
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
    }
    close(sockfd);
}

int main() {
    send_rpc_request("add", 10, 5);
    send_rpc_request("subtract", 10, 5);
    send_rpc_request("multiply", 10, 5);
    send_rpc_request("divide", 10, 5);
    send_rpc_request("divide", 10, 0);
    return 0;
}
