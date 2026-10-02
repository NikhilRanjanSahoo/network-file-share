#include "../include/NetworkClient.h"
#include "../include/protocol.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring> 

bool NetworkClient::authenticate(const std::string& username, const std::string& password) {
    if (!connectToServer()) return false;

    PacketHeader header;
    header.magic = 0xABCD;
    header.opcode = Opcode::AUTH;
    header.filename_len = 0;
    header.payload_size = sizeof(AuthPayload);

    AuthPayload creds = {}; 
    strncpy(creds.username, username.c_str(), sizeof(creds.username) - 1);
    strncpy(creds.password, password.c_str(), sizeof(creds.password) - 1);

    send(sock_fd, &header, sizeof(PacketHeader), 0);
    send(sock_fd, &creds, sizeof(AuthPayload), 0);

    std::cout << "[*] Authenticating as " << username << "...\n";

    PacketHeader ack_header;
    int ack_bytes = recv(sock_fd, &ack_header, sizeof(PacketHeader), 0);

    if (ack_bytes == sizeof(PacketHeader) && ack_header.opcode == Opcode::ACK) {
        if (ack_header.payload_size == 1) {
            std::cout << "[+] Authentication successful! Connected to server.\n";
            disconnect();
            return true;
        } else {
            std::cerr << "[-] Invalid username or password.\n";
        }
    }
    
    disconnect();
    return false;
}

NetworkClient::NetworkClient(const std::string& ip, int port) : server_ip(ip), port(port), sock_fd(-1) {}

NetworkClient::~NetworkClient() {
    disconnect();
}

bool NetworkClient::connectToServer() {
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        std::cerr << "[-] Socket creation error.\n";
        return false;
    }

    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "[-] Invalid address / Address not supported.\n";
        return false;
    }

    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[-] Connection failed.\n";
        return false;
    }

    return true;
}

void NetworkClient::disconnect() {
    if (sock_fd != -1) {
        close(sock_fd);
        sock_fd = -1;
    }
}

bool NetworkClient::upload(const std::string& filepath) {
    if (!connectToServer()) return false;

    size_t pos = filepath.find_last_of("/\\");
    std::string filename = (pos == std::string::npos) ? filepath : filepath.substr(pos + 1);

    if (filename.length() > 255) {
        std::cerr << "[-] Filename exceeds protocol limit.\n";
        disconnect();
        return false;
    }

    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[-] Failed to open " << filepath << "\n";
        disconnect();
        return false;
    }

    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    PacketHeader header;
    header.magic = 0xABCD;
    header.opcode = Opcode::UPLOAD;
    header.filename_len = (uint8_t)filename.length();
    
    std::streamsize chunk_size = std::min(file_size, (std::streamsize)4096);
    std::vector<char> buffer(chunk_size);
    file.read(buffer.data(), chunk_size);
    header.payload_size = chunk_size;

    send(sock_fd, &header, sizeof(PacketHeader), 0);
    send(sock_fd, filename.c_str(), filename.length(), 0);
    send(sock_fd, buffer.data(), chunk_size, 0);

    std::cout << "[*] Uploading " << filename << " (" << chunk_size << " bytes). Waiting for ACK...\n";
    file.close();

    PacketHeader ack_header;
    int ack_bytes = recv(sock_fd, &ack_header, sizeof(PacketHeader), 0);
    
    if (ack_bytes == sizeof(PacketHeader) && ack_header.opcode == Opcode::ACK) {
        std::cout << "[+] Server successfully saved " << filename << "!\n";
    } else {
        std::cerr << "[-] Transfer failed or timed out.\n";
    }

    disconnect();
    return true;
}

bool NetworkClient::download(const std::string& filename, const std::string& dst_filepath) {
    if (!connectToServer()) return false;

    PacketHeader header;
    header.magic = 0xABCD;
    header.opcode = Opcode::DOWNLOAD;
    header.filename_len = (uint8_t)filename.length();
    header.payload_size = 0;

    send(sock_fd, &header, sizeof(PacketHeader), 0);
    send(sock_fd, filename.c_str(), filename.length(), 0);
    
    std::cout << "[*] Requesting " << filename << ". Waiting for acknowledgment...\n";

    PacketHeader ack_header;
    int ack_bytes = recv(sock_fd, &ack_header, sizeof(PacketHeader), 0);

    if (ack_bytes == sizeof(PacketHeader) && ack_header.opcode == Opcode::ACK) {
        uint32_t incoming_size = ack_header.payload_size;
        std::cout << "[+] Incoming file size: " << incoming_size << " bytes.\n";
        
        std::string final_dest = dst_filepath;
        if (final_dest.empty()) {
            mkdir("downloads", 0777); 
            final_dest = "downloads/copy_" + filename;
        }

        std::ofstream outfile(final_dest, std::ios::binary);
        if (outfile.is_open()) {
            std::vector<char> buffer(4096);
            uint32_t total_received = 0;
            
            while (total_received < incoming_size) {
                uint32_t bytes_left = incoming_size - total_received;
                int chunk = recv(sock_fd, buffer.data(), std::min((uint32_t)buffer.size(), bytes_left), 0);
                if (chunk <= 0) break; 
                outfile.write(buffer.data(), chunk);
                total_received += chunk;
            }
            
            std::cout << "[+] Successfully saved to " << final_dest << "\n";
            outfile.close();
        } else {
            std::cerr << "[-] Failed to create local file: " << final_dest << "\n";
        }
    } else {
        std::cerr << "[-] Server rejected download (file may not exist).\n";
    }

    disconnect();
    return true;
}
