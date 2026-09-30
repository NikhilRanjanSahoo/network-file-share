#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "protocol.h"

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[-] Connection failed.\n";
        return 1;
    }

    
    PacketHeader header;
    header.magic = 0xABCD;
    header.opcode = Opcode::UPLOAD;
    header.payload_size = 1024; 

    
    send(sock, &header, sizeof(PacketHeader), 0);
    std::cout << "[*] Sent UPLOAD header to server.\n";

    close(sock);
    return 0;
}
