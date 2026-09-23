CXX ?= g++

# C++17 标准
CXXFLAGS = -std=c++17 -Wall

DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2
endif

# 使用 wildcard 自动扫描当前目录，以及各个子目录下的所有 .cpp 文件
SRCS = $(wildcard *.cpp) \
       $(wildcard log/*.cpp) \
       $(wildcard timer/*.cpp) \
       $(wildcard http/*.cpp) \
       $(wildcard CGImysql/*.cpp) \
       $(wildcard buffer/*.cpp) \
       $(wildcard epoller/*.cpp) \
	   $(wildcard router/*.cpp)

# 生成的可执行文件名称
TARGET = server

# 链接器参数：多线程库和 MySQL 客户端库
OBJS = $(SRCS)
LIBS = -lpthread -lmysqlclient

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) $(LIBS)

clean:
	rm -f $(TARGET)