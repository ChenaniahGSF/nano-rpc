# 编译器和编译选项
CC = gcc
CFLAGS = -I./src/ -I./third_party/C-Thread-Pool/ -I./third_party/nanopb/ -lpthread

# 源文件和目标文件
SERVER_SOURCES = src/server.c src/rpc.pb.c third_party/C-Thread-Pool/thpool.c third_party/nanopb/pb_common.c third_party/nanopb/pb_decode.c third_party/nanopb/pb_encode.c
CLIENT_SOURCES = src/client.c src/rpc.pb.c third_party/C-Thread-Pool/thpool.c third_party/nanopb/pb_common.c third_party/nanopb/pb_decode.c third_party/nanopb/pb_encode.c

SERVER_TARGET = build/server
CLIENT_TARGET = build/client

# 创建 build 目录
$(shell mkdir -p build)

# 默认目标
all: $(SERVER_TARGET) $(CLIENT_TARGET)

# 编译服务器程序
$(SERVER_TARGET): $(SERVER_SOURCES)
	$(CC) $(CFLAGS) $^ -o $@

# 编译客户端程序
$(CLIENT_TARGET): $(CLIENT_SOURCES)
	$(CC) $(CFLAGS) $^ -o $@

# 清理目标
clean:
	rm -f $(SERVER_TARGET) $(CLIENT_TARGET)
