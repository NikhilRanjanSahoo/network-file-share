#include <iostream>
#include <fstream>
#include <vector>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "protocol.h"

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
        std::cout << "[+] Valid Packet. Opcode: " << (int)header.opcode << ", Size: " << header.payload_size << " bytes\n";
        
        if (header.opcode == Opcode::UPLOAD) {
            
            std::ofstream outfile("uploaded_file.txt", std::ios::binary);
            
            if (outfile.is_open()) {
                std::vector<char> buffer(4096);
                uint32_t total_received = 0;
                
                
                while (total_received < header.payload_size) {
                    uint32_t bytes_left = header.payload_size - total_received;
                    int chunk = recv(client_fd, buffer.data(), std::min((uint32_t)buffer.size(), bytes_left), 0);
                    
                    if (chunk <= 0) break; 
                    
                    outfile.write(buffer.data(), chunk);
                    total_received += chunk;
                }
                
                std::cout << "[+] Successfully saved " << total_received << " bytes to uploaded_file.txt\n";
                outfile.close();
            }
        }
    } else {
        std::cout << "[-] Invalid Header. Dropping connection.\n";
    }

    close(client_fd);
    close(server_fd);
    return 0;
}
