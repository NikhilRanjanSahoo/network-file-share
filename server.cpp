#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <iostream>
#include <vector>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <thread> 
#include <string>
#include "protocol.h"

void handle_upload(int client_fd, const std::string& filename, uint32_t payload_size) {
    int file_fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (file_fd < 0) return;

    struct flock file_lock = {};
    file_lock.l_type = F_WRLCK; 
    file_lock.l_whence = SEEK_SET;
    
    std::cout << "[Thread " << std::this_thread::get_id() << "] Write lock acquired for " << filename << "...\n";
    fcntl(file_fd, F_OFD_SETLKW, &file_lock); 
    
    std::vector<char> buffer(4096);
    uint32_t total_received = 0;
    
    while (total_received < payload_size) {
        uint32_t bytes_left = payload_size - total_received;
        int chunk = recv(client_fd, buffer.data(), std::min((uint32_t)buffer.size(), bytes_left), 0);
        if (chunk <= 0) break;
        write(file_fd, buffer.data(), chunk);
        total_received += chunk;
    }
    
    file_lock.l_type = F_UNLCK;
    fcntl(file_fd, F_OFD_SETLK, &file_lock);
    std::cout << "[Thread " << std::this_thread::get_id() << "] Saved " << filename << ".\n";
    
    close(file_fd);

    PacketHeader ack_header;
    ack_header.magic = 0xABCD;
    ack_header.opcode = Opcode::ACK;
    ack_header.filename_len = 0;
    ack_header.payload_size = 0; 
    send(client_fd, &ack_header, sizeof(PacketHeader), 0);
}

void handle_download(int client_fd, const std::string& filename) {
    int file_fd = open(filename.c_str(), O_RDONLY);
    if (file_fd < 0) {
        std::cerr << "[-] File " << filename << " does not exist on server.\n";
        return; 
    }

    struct flock file_lock = {};
    file_lock.l_type = F_RDLCK; 
    file_lock.l_whence = SEEK_SET;

    std::cout << "[Thread " << std::this_thread::get_id() << "] Read lock acquired for " << filename << "...\n";
    fcntl(file_fd, F_OFD_SETLKW, &file_lock); 

    off_t file_size = lseek(file_fd, 0, SEEK_END);
    lseek(file_fd, 0, SEEK_SET);

    PacketHeader ack_header;
    ack_header.magic = 0xABCD;
    ack_header.opcode = Opcode::ACK;
    ack_header.filename_len = 0;
    ack_header.payload_size = (uint32_t)file_size;
    send(client_fd, &ack_header, sizeof(PacketHeader), 0);

    std::vector<char> buffer(4096);
    ssize_t bytes_read;
    while ((bytes_read = read(file_fd, buffer.data(), buffer.size())) > 0) {
        send(client_fd, buffer.data(), bytes_read, 0);
    }

    file_lock.l_type = F_UNLCK;
    fcntl(file_fd, F_OFD_SETLK, &file_lock);
    std::cout << "[Thread " << std::this_thread::get_id() << "] Streamed " << filename << ".\n";
    
    close(file_fd);
}

void handle_client(int client_fd) {
    PacketHeader header;
    int bytes_received = recv(client_fd, &header, sizeof(PacketHeader), 0);

    if (bytes_received == sizeof(PacketHeader) && header.magic == 0xABCD) {
        
        
        std::string filename(header.filename_len, '\0');
        if (header.filename_len > 0) {
            recv(client_fd, &filename[0], header.filename_len, 0);
        }

        switch (header.opcode) {
            case Opcode::UPLOAD:
                handle_upload(client_fd, filename, header.payload_size);
                break;
            case Opcode::DOWNLOAD:
                handle_download(client_fd, filename);
                break;
            default:
                break;
        }
    }
    close(client_fd);
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);
    address.sin_addr.s_addr = INADDR_ANY;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, 5);
    
    std::cout << "[*] Multithreaded Server listening on port 8080...\n";
    
    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd >= 0) {
            std::thread(handle_client, client_fd).detach();
        }
    }

    close(server_fd);
    return 0;
}
