#include "../include/TransferService.h"
#include "../include/protocol.h"
#include <sys/socket.h>
#include <iostream>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>

TransferService::TransferService() {}
TransferService::~TransferService() {}

bool TransferService::receiveFile(int socket_fd, const std::string& filename, uint32_t payload_size) {
    
    std::string filepath = "server_storage/public/" + filename;
    
    int file_fd = fileManager.openFile(filepath, O_WRONLY | O_CREAT | O_TRUNC);
    if (file_fd < 0) {
        std::cerr << "[-] Failed to open file for writing: " << filepath << "\n";
        return false;
    }

    fileManager.acquireWriteLock(file_fd);
    std::cout << "[*] Write lock acquired for " << filepath << ". Receiving data...\n";

    std::vector<char> buffer(4096);
    uint32_t total_received = 0;

    while (total_received < payload_size) {
        uint32_t bytes_left = payload_size - total_received;
        int chunk = recv(socket_fd, buffer.data(), std::min((uint32_t)buffer.size(), bytes_left), 0);
        
        if (chunk <= 0) break;
        
        fileManager.writeFile(file_fd, buffer, chunk);
        total_received += chunk;
    }

    fileManager.releaseLock(file_fd);
    close(file_fd);
    std::cout << "[+] Saved " << filepath << ".\n";

    
    PacketHeader ack_header;
    ack_header.magic = 0xABCD;
    ack_header.opcode = Opcode::ACK;
    ack_header.filename_len = 0;
    ack_header.payload_size = 0;
    send(socket_fd, &ack_header, sizeof(PacketHeader), 0);

    return total_received == payload_size;
}

bool TransferService::sendFile(int socket_fd, const std::string& filepath) {
    int file_fd = fileManager.openFile(filepath, O_RDONLY);
    if (file_fd < 0) {
        std::cerr << "[-] File does not exist: " << filepath << "\n";
        return false;
    }

    fileManager.acquireReadLock(file_fd);
    std::cout << "[*] Read lock acquired for " << filepath << ". Streaming data...\n";

    off_t file_size = lseek(file_fd, 0, SEEK_END);
    lseek(file_fd, 0, SEEK_SET);

    
    PacketHeader ack_header;
    ack_header.magic = 0xABCD;
    ack_header.opcode = Opcode::ACK;
    ack_header.filename_len = 0;
    ack_header.payload_size = (uint32_t)file_size;
    send(socket_fd, &ack_header, sizeof(PacketHeader), 0);

    std::vector<char> buffer(4096);
    ssize_t bytes_read;
    
    while ((bytes_read = fileManager.readFile(file_fd, buffer, buffer.size())) > 0) {
        send(socket_fd, buffer.data(), bytes_read, 0);
    }

    fileManager.releaseLock(file_fd);
    close(file_fd);
    std::cout << "[+] Streamed " << filepath << " successfully.\n";

    return true;
}

std::string TransferService::calculateHash(const std::string& filepath) {
    
    return "hash_pending";
}
