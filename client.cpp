#include <iostream>
#include <fstream>
#include <vector>
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

    
    std::ifstream file("test_payload.txt", std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[-] Failed to open test_payload.txt\n";
        close(sock);
        return 1;
    }

    
    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    
    std::streamsize chunk_size = std::min(file_size, (std::streamsize)4096);
    std::vector<char> buffer(chunk_size);
    file.read(buffer.data(), chunk_size);

    
    PacketHeader header;
    header.opcode = Opcode::UPLOAD;
    header.payload_size = chunk_size;

    
    send(sock, &header, sizeof(PacketHeader), 0);
    send(sock, buffer.data(), chunk_size, 0);

    std::cout << "[*] Sent UPLOAD header and " << chunk_size << " bytes of binary data.\n";

    file.close();
    close(sock);
    return 0;
}
