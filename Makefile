# Layout: include/*.h  src/*.cpp  -> bin/server, bin/client
# Needs: libsqlite3-dev libssl-dev libncurses-dev  (Debian/Ubuntu names)
.RECIPEPREFIX = >
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pthread
CXXFLAGS += -Iinclude

COMMON = src/net_io.cpp
SERVER_SRCS = src/main_server.cpp src/NetworkServer.cpp src/Database.cpp \
              src/AuthenticationService.cpp src/FileManager.cpp src/TransferService.cpp \
              src/PermissionService.cpp src/SessionManager.cpp src/Crypto.cpp \
              src/SetupWizard.cpp $(COMMON)
CLIENT_SRCS = src/main_client.cpp src/NetworkClient.cpp $(COMMON)

all: bin/server bin/client

bin/server: $(SERVER_SRCS) $(wildcard include/*.h) | bin
> $(CXX) $(CXXFLAGS) $(SERVER_SRCS) -o $@ -lsqlite3 -lcrypto

bin/client: $(CLIENT_SRCS) $(wildcard include/*.h) | bin
> $(CXX) $(CXXFLAGS) $(CLIENT_SRCS) -o $@ -lncurses

bin:
> mkdir -p bin

clean:
> rm -rf bin

.PHONY: all clean
