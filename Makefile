
CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++17
LDFLAGS_SERVER = -pthread -lsqlite3 -lssl -lcrypto


SERVER_BIN = file_server
SERVER_SRC = src/main_server.cpp src/NetworkServer.cpp src/TransferService.cpp src/FileManager.cpp src/Database.cpp src/AuthenticationService.cpp src/PermissionService.cpp


CLIENT_BIN = file_client
CLIENT_SRC = src/main_client.cpp src/NetworkClient.cpp


all: $(SERVER_BIN) $(CLIENT_BIN)


$(SERVER_BIN): $(SERVER_SRC)
	$(CXX) $(CXXFLAGS) $(SERVER_SRC) -o $(SERVER_BIN) $(LDFLAGS_SERVER)
	@echo "[+] Server built successfully: ./$(SERVER_BIN)"


$(CLIENT_BIN): $(CLIENT_SRC)
	$(CXX) $(CXXFLAGS) $(CLIENT_SRC) -o $(CLIENT_BIN)
	@echo "[+] Client built successfully: ./$(CLIENT_BIN)"


clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN)
	@echo "[+] Cleaned up executable files."


.PHONY: all clean
