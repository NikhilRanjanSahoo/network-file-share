#include <iostream>
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

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, 5);
    
    std::cout << "[*] Server listening on port 8080...\n";
    
    int client_fd = accept(server_fd, nullptr, nullptr);
    std::cout << "[*] Client connected!\n";

    
    PacketHeader header;
    int bytes_received = recv(client_fd, &header, sizeof(PacketHeader), 0);

    if (bytes_received == sizeof(PacketHeader)) {
        if (header.magic == 0xABCD) {
            std::cout << "[+] Valid Packet Received.\n";
            std::cout << "    Opcode: " << (int)header.opcode << "\n";
            std::cout << "    Payload Size: " << header.payload_size << " bytes\n";
        } else {
            std::cout << "[-] Invalid Magic Number. Dropping connection.\n";
        }
    }

    close(client_fd);
    close(server_fd);
    return 0;
}
