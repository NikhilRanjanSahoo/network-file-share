#include "../include/NetworkServer.h"
#include "../include/TransferService.h"
#include "../include/protocol.h"
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

        switch (header.opcode) {
            case Opcode::UPLOAD:
                transferService.receiveFile(client_fd, filename, header.payload_size);
                break;
            case Opcode::DOWNLOAD:
                transferService.sendFile(client_fd, "server_storage/public/" + filename);
                break;
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
