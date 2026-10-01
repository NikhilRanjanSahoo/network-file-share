#include <iostream>
#include <vector>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h> 
#include "protocol.h"

void handle_upload(int client_fd, uint32_t payload_size) {
    
    int file_fd = open("uploaded_file.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (file_fd < 0) {
        std::cerr << "[-] Failed to open output file.\n";
        return;
    }

    
    struct flock file_lock = {};
    file_lock.l_type = F_WRLCK;    
    file_lock.l_whence = SEEK_SET; 
    file_lock.l_start = 0;         
    file_lock.l_len = 0;           

    std::cout << "[*] Requesting exclusive write lock on file...\n";
    
    
    fcntl(file_fd, F_SETLKW, &file_lock); 
    std::cout << "[+] Lock acquired. Saving binary data...\n";

    
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
    fcntl(file_fd, F_SETLK, &file_lock);
    std::cout << "[+] File saved. Lock released.\n";
    
    close(file_fd);
}

void handle_download(int client_fd) {
    std::cout << "[*] DOWNLOAD command received (Pending Implementation).\n";
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
    
    std::cout << "[*] Server listening on port 8080...\n";
    
    int client_fd = accept(server_fd, nullptr, nullptr);
    std::cout << "[*] Client connected!\n";

    PacketHeader header;
    int bytes_received = recv(client_fd, &header, sizeof(PacketHeader), 0);

    if (bytes_received == sizeof(PacketHeader) && header.magic == 0xABCD) {
        
        switch (header.opcode) {
            case Opcode::UPLOAD:
                handle_upload(client_fd, header.payload_size);
                break;
            case Opcode::DOWNLOAD:
                handle_download(client_fd);
                break;
            default:
                std::cout << "[-] Unknown opcode received.\n";
                break;
        }
    } else {
        std::cout << "[-] Invalid Header. Dropping connection.\n";
    }

    close(client_fd);
    close(server_fd);
    return 0;
}
