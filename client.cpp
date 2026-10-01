#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h> 
#include "protocol.h"

int main(int argc, char* argv[]) {
    
    if (argc < 3 || argc > 4) {
        std::cerr << "Usage: ./client <upload|download> <src_filepath> [dst_filepath]\n";
        return 1;
    }

    std::string action = argv[1];
    std::string src_filepath = argv[2];

    
    size_t pos = src_filepath.find_last_of("/\\");
    std::string filename = (pos == std::string::npos) ? src_filepath : src_filepath.substr(pos + 1);

    if (filename.length() > 255) {
        std::cerr << "[-] Filename exceeds 255 character protocol limit.\n";
        return 1;
    }

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
    header.filename_len = (uint8_t)filename.length(); 

    if (action == "upload") {
        if (argc == 4) {
            std::cerr << "[-] Upload command only takes one file argument.\n";
            close(sock);
            return 1;
        }
        
        header.opcode = Opcode::UPLOAD;
        
        std::ifstream file(src_filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "[-] Failed to open " << src_filepath << "\n";
            close(sock);
            return 1;
        }

        std::streamsize file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::streamsize chunk_size = std::min(file_size, (std::streamsize)4096);
        std::vector<char> buffer(chunk_size);
        file.read(buffer.data(), chunk_size);

        header.payload_size = chunk_size;

        
        send(sock, &header, sizeof(PacketHeader), 0);
        send(sock, filename.c_str(), filename.length(), 0);
        send(sock, buffer.data(), chunk_size, 0);

        std::cout << "[*] Uploading " << filename << " (" << chunk_size << " bytes). Waiting for ACK...\n";
        file.close();
        
        PacketHeader ack_header;
        int ack_bytes = recv(sock, &ack_header, sizeof(PacketHeader), 0);
        
        if (ack_bytes == sizeof(PacketHeader) && ack_header.opcode == Opcode::ACK) {
            std::cout << "[+] Server successfully saved " << filename << "!\n";
        }

    } else if (action == "download") {
        header.opcode = Opcode::DOWNLOAD;
        header.payload_size = 0; 

        std::string dst_filepath;
        if (argc == 4) {
            dst_filepath = argv[3];
        } else {
            mkdir("downloads", 0777); 
            dst_filepath = "downloads/copy_" + filename;
        }

        
        send(sock, &header, sizeof(PacketHeader), 0);
        send(sock, filename.c_str(), filename.length(), 0);
        
        std::cout << "[*] Requesting " << filename << ". Waiting for acknowledgment...\n";
        
        PacketHeader ack_header;
        int ack_bytes = recv(sock, &ack_header, sizeof(PacketHeader), 0);
        
        if (ack_bytes == sizeof(PacketHeader) && ack_header.opcode == Opcode::ACK) {
            uint32_t incoming_size = ack_header.payload_size;
            std::cout << "[+] Incoming file size: " << incoming_size << " bytes.\n";
            
            std::ofstream outfile(dst_filepath, std::ios::binary);
            
            if (outfile.is_open()) {
                std::vector<char> buffer(4096);
                uint32_t total_received = 0;
                
                while (total_received < incoming_size) {
                    uint32_t bytes_left = incoming_size - total_received;
                    int chunk = recv(sock, buffer.data(), std::min((uint32_t)buffer.size(), bytes_left), 0);
                    if (chunk <= 0) break; 
                    outfile.write(buffer.data(), chunk);
                    total_received += chunk;
                }
                
                std::cout << "[+] Successfully saved to " << dst_filepath << "\n";
                outfile.close();
            } else {
                std::cerr << "[-] Failed to create local file: " << dst_filepath << "\n";
            }
        } else {
            std::cerr << "[-] Server rejected download or timed out.\n";
        }
        
    }

    close(sock);
    return 0;
}
