#include "../include/NetworkServer.h"
#include "../include/TransferService.h"
#include "../include/protocol.h"
#include "../include/AuthenticationService.h"
#include "../include/PermissionService.h"
#include "../include/Database.h"
#include <iostream>
#include <thread>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstring>

NetworkServer::NetworkServer(int port) : port(port), server_fd(-1) {
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;
}

NetworkServer::~NetworkServer() {
    stop();
}

bool NetworkServer::start() {
    
    if (!db.connect("database/file_sharing.db")) {
        std::cerr << "[-] Database connection failed.\n";
        return false;
    }
    
    if (!db.initializeTables()) {
        std::cerr << "[-] Failed to initialize database tables.\n";
        return false;
    }
    db.saveUser("nikhil_2341019074", "iter123", "Student", "server_storage/users/nikhil");//test
    std::cout << "[+] SQLite Database file_sharing.db initialized successfully.\n";

    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "[-] Failed to create socket.\n";
        return false;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "[-] Bind failed on port " << port << ".\n";
        return false;
    }

    if (listen(server_fd, 5) < 0) {
        std::cerr << "[-] Listen failed.\n";
        return false;
    }

    std::cout << "[*] OOP Network Server listening on port " << port << "...\n";
    return true;
}

void NetworkServer::listenForClients() {
    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd >= 0) {
            std::cout << "[*] New client connected! Spawning thread...\n";
            
            std::thread(&NetworkServer::handleClient, this, client_fd).detach();
        }
    }
}

void NetworkServer::handleClient(int client_fd) {
    PacketHeader header;
    int bytes_received = recv(client_fd, &header, sizeof(PacketHeader), 0);

    if (bytes_received == sizeof(PacketHeader) && header.magic == 0xABCD) {
        
        std::string filename(header.filename_len, '\0');
        if (header.filename_len > 0) {
            recv(client_fd, &filename[0], header.filename_len, 0);
        }

        TransferService transferService;
        AuthenticationService authService(db); 
        FileManager fm;

        switch (header.opcode) {
            case Opcode::AUTH: {
    		AuthPayload auth_payload;
    		memset(&auth_payload, 0, sizeof(AuthPayload));

    
    		int bytes_received = recv(client_fd, &auth_payload, sizeof(AuthPayload), MSG_WAITALL);
    		if (bytes_received != sizeof(AuthPayload)) {
        		std::cerr << "[-] Incomplete AuthPayload received from client.\n";
        		break;
    		}

    
    		auth_payload.username[sizeof(auth_payload.username) - 1] = '\0';
    		auth_payload.password[sizeof(auth_payload.password) - 1] = '\0';

    
    		AuthenticationService auth(db);
    		bool is_authenticated = auth.authenticate(auth_payload.username, auth_payload.password);

    		std::string response_payload;
    		PacketHeader ack_header;
    		memset(&ack_header, 0, sizeof(PacketHeader));
    		ack_header.magic = 0xABCD;
    		ack_header.opcode = Opcode::ACK;

    		if (is_authenticated) {
        		std::string role = auth.getUserRole();
        		response_payload = "SUCCESS:" + role;
        		std::cout << "[+] User '" << auth_payload.username << "' logged in. Role: " << role << "\n";
    		} else {
        		response_payload = "FAILURE:Invalid credentials";
    		}

    		ack_header.payload_size = static_cast<uint32_t>(response_payload.length());

    		
    		send(client_fd, &ack_header, sizeof(PacketHeader), 0);
    		send(client_fd, response_payload.c_str(), response_payload.length(), 0);
    		break;
	    } 

            case Opcode::DELETE_FILE: {
                std::cout << "[*] Client requested to delete: " << filename << "\n";
                
                std::string token(header.session_token);
                std::string active_role = "Guest";
                {
                    std::lock_guard<std::mutex> lock(session_mutex);
                    if (active_sessions.count(token)) active_role = active_sessions[token];
                }

                if (!PermissionService::canDelete(active_role)) {
                    std::cerr << "[-] Security Block: Role '" << active_role << "' attempted unauthorized DELETE.\n";
                    db.saveTransfer(1, filename, "DELETE", 0, "DENIED_RBAC");
                    PacketHeader ack_header = {0xABCD, Opcode::ACK, 0, 0};
                    send(client_fd, &ack_header, sizeof(PacketHeader), 0);
                    break;
                }

                bool success = fm.deleteFile("server_storage/public/" + filename);
                db.saveTransfer(1, filename, "DELETE", 0, success ? "SUCCESS" : "FAILED");
                
                PacketHeader ack_header = {0xABCD, Opcode::ACK, 0, (uint32_t)(success ? 1 : 0)};
                send(client_fd, &ack_header, sizeof(PacketHeader), 0);
                break;
            }

            case Opcode::RENAME_FILE: {
                std::cout << "[*] Client requested rename: " << filename << "\n";

                std::string token(header.session_token);
                std::string active_role = "Guest";
                {
                    std::lock_guard<std::mutex> lock(session_mutex);
                    if (active_sessions.count(token)) active_role = active_sessions[token];
                }

                if (!PermissionService::canRename(active_role)) {
                    std::cerr << "[-] Security Block: Role '" << active_role << "' attempted unauthorized RENAME.\n";
                    db.saveTransfer(1, filename, "RENAME", 0, "DENIED_RBAC");
                    PacketHeader ack_header = {0xABCD, Opcode::ACK, 0, 0};
                    send(client_fd, &ack_header, sizeof(PacketHeader), 0);
                    break;
                }

                size_t delim_pos = filename.find('|');
                bool success = false;
                
                if (delim_pos != std::string::npos) {
                    std::string old_name = filename.substr(0, delim_pos);
                    std::string new_name = filename.substr(delim_pos + 1);
                    success = fm.renameFile("server_storage/public/" + old_name, "server_storage/public/" + new_name);
                }
                
                db.saveTransfer(1, filename, "RENAME", 0, success ? "SUCCESS" : "FAILED"); 
                
                PacketHeader ack_header = {0xABCD, Opcode::ACK, 0, (uint32_t)(success ? 1 : 0)};
                send(client_fd, &ack_header, sizeof(PacketHeader), 0);
                break;
            }

            case Opcode::LIST: {
                std::cout << "[*] Client requested directory list.\n";
                std::string list_output = fm.listDirectory("server_storage/public");
                
                PacketHeader ack_header = {0xABCD, Opcode::ACK, 0, (uint32_t)list_output.length()};
                send(client_fd, &ack_header, sizeof(PacketHeader), 0);
                if (!list_output.empty()) {
                    send(client_fd, list_output.c_str(), list_output.length(), 0);
                }
                break;
            }

            case Opcode::UPLOAD: {
                transferService.receiveFile(client_fd, filename, header.payload_size);
                
                std::string path = "server_storage/public/" + filename;
                std::string hash = fm.calculateSHA256(path);
                
                std::cout << "[+] Upload complete. SHA-256 Integrity: " << hash << "\n";
                
                
                db.saveFileRecord(1, filename, path, header.payload_size, hash);              
                db.saveTransfer(1, filename, "UPLOAD", header.payload_size, "SUCCESS");
                
                PacketHeader ack_header = {0xABCD, Opcode::ACK, 0, 0};
                send(client_fd, &ack_header, sizeof(PacketHeader), 0);
                break;
            }

            case Opcode::DOWNLOAD: {
                transferService.sendFile(client_fd, "server_storage/public/" + filename);
                db.saveTransfer(1, filename, "DOWNLOAD", 0, "SUCCESS");
                break;
            }
            
            case Opcode::FILE_INFO: {
                std::cout << "[*] Client requested info for: " << filename << "\n";
                
                std::string info = fm.getFileInfo("server_storage/public/" + filename);
                
                PacketHeader ack_header;
                memset(&ack_header, 0, sizeof(PacketHeader));
                ack_header.magic = 0xABCD;
                ack_header.opcode = Opcode::ACK;
                ack_header.payload_size = (uint32_t)info.length();
                
                send(client_fd, &ack_header, sizeof(PacketHeader), 0);
                
                if (!info.empty()) {
                    send(client_fd, info.c_str(), info.length(), 0);
                }
                break;
            }
            
            case Opcode::CREATE_DIR: {
                std::cout << "[*] Client requested to create directory: " << filename << "\n";
                bool success = fm.createDir("server_storage/" + filename); 
                
                PacketHeader ack_header;
                memset(&ack_header, 0, sizeof(PacketHeader));
                ack_header.magic = 0xABCD;
                ack_header.opcode = Opcode::ACK;
                ack_header.payload_size = success ? 1 : 0;
                
                send(client_fd, &ack_header, sizeof(PacketHeader), 0);
                break;
            }

            case Opcode::REMOVE_DIR: {
                std::cout << "[*] Client requested to remove directory: " << filename << "\n";
                bool success = fm.removeDir("server_storage/" + filename);
                
                PacketHeader ack_header;
                memset(&ack_header, 0, sizeof(PacketHeader));
                ack_header.magic = 0xABCD;
                ack_header.opcode = Opcode::ACK;
                ack_header.payload_size = success ? 1 : 0;
                
                send(client_fd, &ack_header, sizeof(PacketHeader), 0);
                break;
            }
            
            case Opcode::SEARCH: {
                std::cout << "[*] Client requested search for: " << filename << "\n";
                std::string search_results = fm.searchFiles("server_storage/", filename);
                
                PacketHeader ack_header;
                memset(&ack_header, 0, sizeof(PacketHeader));
                ack_header.magic = 0xABCD;
                ack_header.opcode = Opcode::ACK;
                ack_header.payload_size = (uint32_t)search_results.length();
                
                send(client_fd, &ack_header, sizeof(PacketHeader), 0);
                
                if (!search_results.empty()) {
                    send(client_fd, search_results.c_str(), search_results.length(), 0);
                }
                break;
            }
            
            case Opcode::HISTORY: {
    
    		char session_user[33] = {0};
    		strncpy(session_user, header.session_token, 32);
    		std::string role = db.getUserRole(session_user); 

    		std::cout << "[*] User '" << session_user << "' (" << role << ") requested transfer history.\n";

    		if (role != "Admin" && role != "Faculty") {
        		std::string err_msg = "[-] Security Block: Log access restricted to Admin and Faculty roles.\n";
        		PacketHeader ack;
        		memset(&ack, 0, sizeof(PacketHeader));
        		ack.magic = 0xABCD;
        		ack.opcode = Opcode::ACK;
        		ack.payload_size = err_msg.length();

        		send(client_fd, &ack, sizeof(PacketHeader), 0);
        		send(client_fd, err_msg.c_str(), err_msg.length(), 0);
        		break;
    		}


    		std::string history_data = db.getTransferHistoryLogs(); 
    		if (history_data.empty()) {
        		history_data = "[No transfer history recorded yet.]\n";
    		}

    		PacketHeader ack;
    		memset(&ack, 0, sizeof(PacketHeader));
    		ack.magic = 0xABCD;
    		ack.opcode = Opcode::ACK;
    		ack.payload_size = history_data.length();

    		send(client_fd, &ack, sizeof(PacketHeader), 0);
    		send(client_fd, history_data.c_str(), history_data.length(), 0);
    		break;
	    }
	    
            default: {
                std::cerr << "[-] Unknown opcode received.\n";
                break;
            }
        } 
        
        close(client_fd);
    } 
}
void NetworkServer::stop() {
    if (server_fd != -1) {
        close(server_fd);
        server_fd = -1;
        std::cout << "[*] Server shut down.\n";
    }
}

std::string NetworkServer::generateSessionToken() {
    const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string token = "";
    for (int i = 0; i < 31; ++i) token += charset[rand() % (sizeof(charset) - 1)];
    return token;
}
