#include "../include/NetworkServer.h"
#include "../include/TransferService.h"
#include "../include/protocol.h"
#include "../include/AuthenticationService.h"
#include <iostream>
#include <thread>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

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
                AuthPayload creds;
                recv(client_fd, &creds, sizeof(AuthPayload), 0);
                
                bool success = authService.authenticate(creds.username, creds.password);
                
                PacketHeader ack_header;
                ack_header.magic = 0xABCD;
                ack_header.opcode = Opcode::ACK;
                ack_header.filename_len = 0;
                ack_header.payload_size = success ? 1 : 0; 
                
                send(client_fd, &ack_header, sizeof(PacketHeader), 0);
                break;
            }
            
            case Opcode::LIST: {
                std::cout << "[*] Client requested directory list.\n";
                std::string list_output = fm.listDirectory("server_storage/public");
                
                PacketHeader ack_header;
                ack_header.magic = 0xABCD;
                ack_header.opcode = Opcode::ACK;
                ack_header.filename_len = 0;
                ack_header.payload_size = list_output.length();
                
                
                send(client_fd, &ack_header, sizeof(PacketHeader), 0);
                if (!list_output.empty()) {
                    send(client_fd, list_output.c_str(), list_output.length(), 0);
                }
                break;
            }
            case Opcode::UPLOAD:{
            	std::cout << "[*] Client requested upload.\n";
                transferService.receiveFile(client_fd, filename, header.payload_size);
                db.saveTransfer(1, filename, "UPLOAD", header.payload_size, "SUCCESS");
                break;
                }
            case Opcode::DOWNLOAD:{
            	std::cout << "[*] Client requested download.\n";
                transferService.sendFile(client_fd, "server_storage/public/" + filename);
                db.saveTransfer(1, filename, "DOWNLOAD", 0, "SUCCESS");
                break;
                }
                
            case Opcode::DELETE_FILE: {
            	std::cout<< "[*] Client requested to delete: " << filename << "\n";
                bool success = fm.deleteFile("server_storage/public/" + filename);
                PacketHeader ack_header = {0xABCD, Opcode::ACK, 0, (uint32_t)(success ? 1 : 0)};
                send(client_fd, &ack_header, sizeof(PacketHeader), 0);
                db.saveTransfer(1, filename, "DELETE", 0, success ? "SUCCESS" : "FAILED");
                break;
            }
            case Opcode::RENAME_FILE: {
            	std::cout << "[*] Client requested rename: " << filename << "\n";
                size_t delim_pos = filename.find('|');
                bool success = false;
                if (delim_pos != std::string::npos) {
                    std::string old_name = filename.substr(0, delim_pos);
                    std::string new_name = filename.substr(delim_pos + 1);
                    success = fm.renameFile("server_storage/public/" + old_name, "server_storage/public/" + new_name);
                }
                PacketHeader ack_header = {0xABCD, Opcode::ACK, 0, (uint32_t)(success ? 1 : 0)};
                send(client_fd, &ack_header, sizeof(PacketHeader), 0);
                db.saveTransfer(1, filename, "RENAME", 0, success ? "SUCCESS" : "FAILED");
                break;
            }
            default:
                std::cerr << "[-] Unknown opcode received.\n";
                break;
        }
    }
    close(client_fd);
}

void NetworkServer::stop() {
    if (server_fd != -1) {
        close(server_fd);
        server_fd = -1;
        std::cout << "[*] Server shut down.\n";
    }
}
