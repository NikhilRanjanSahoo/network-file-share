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


void loadSessionToken(PacketHeader& header) {
    std::ifstream session_file(".session");
    if (session_file.is_open()) {
        session_file.read(header.session_token, 31);
        session_file.close();
    }
}


bool NetworkClient::authenticate(const std::string& username, const std::string& password) {
    if (!connectToServer()) return false;

    PacketHeader header;
    memset(&header, 0, sizeof(PacketHeader));
    header.magic = 0xABCD;
    header.opcode = Opcode::AUTH; 
    
    AuthPayload payload;
    memset(&payload, 0, sizeof(AuthPayload));
    strncpy(payload.username, username.c_str(), 31);
    strncpy(payload.password, password.c_str(), 31);
    
    header.payload_size = sizeof(AuthPayload);

    send(sock_fd, &header, sizeof(PacketHeader), 0);
    send(sock_fd, &payload, sizeof(AuthPayload), 0);

    PacketHeader ack_header;
    if (recv(sock_fd, &ack_header, sizeof(PacketHeader), 0) > 0 && ack_header.opcode == Opcode::ACK) {
        if (ack_header.payload_size > 0) {
            std::vector<char> buffer(ack_header.payload_size + 1, '\0');
            recv(sock_fd, buffer.data(), ack_header.payload_size, 0);
            
            std::string response(buffer.data()); 
            
            if (response.rfind("SUCCESS:", 0) == 0) {
                
                size_t colon_pos = response.find(':');
                if (colon_pos != std::string::npos) {
                    current_role = response.substr(colon_pos + 1);
                }
                
                std::ofstream session_out(".session");
                if (session_out.is_open()) {
                    session_out << username;
                    session_out.close();
                }
                
                disconnect();
                return true;
            }
        }
    }
    
    disconnect();
    return false; 
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
    memset(&header, 0, sizeof(PacketHeader));
    header.magic = 0xABCD;
    header.opcode = Opcode::UPLOAD;
    header.filename_len = (uint8_t)filename.length();
    loadSessionToken(header); 
    
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
    memset(&header, 0, sizeof(PacketHeader));
    header.magic = 0xABCD;
    header.opcode = Opcode::DOWNLOAD;
    header.filename_len = (uint8_t)filename.length();
    loadSessionToken(header); 

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

bool NetworkClient::listFiles() {
    if (!connectToServer()) return false;

    PacketHeader header;
    memset(&header, 0, sizeof(PacketHeader));
    header.magic = 0xABCD;
    header.opcode = Opcode::LIST;
    loadSessionToken(header); 

    send(sock_fd, &header, sizeof(PacketHeader), 0);

    PacketHeader ack_header;
    int ack_bytes = recv(sock_fd, &ack_header, sizeof(PacketHeader), 0);

    if (ack_bytes == sizeof(PacketHeader) && ack_header.opcode == Opcode::ACK) {
        uint32_t incoming_size = ack_header.payload_size;
        
        if (incoming_size > 0) {
            
            std::vector<char> buffer(incoming_size + 1, '\0');
            uint32_t total_received = 0;
            
            while (total_received < incoming_size) {
                int chunk = recv(sock_fd, buffer.data() + total_received, incoming_size - total_received, 0);
                if (chunk <= 0) break;
                total_received += chunk;
            }
            
            std::cout << "\nServer files:\n" << buffer.data() << "\n";
        } else {
            std::cout << "\n[Empty Directory]\n";
        }
    } else {
        std::cerr << "[-] Server failed to respond to LIST command.\n";
    }

    disconnect();
    return true;
}

bool NetworkClient::deleteRemoteFile(const std::string& filename) {
    if (!connectToServer()) return false;

    PacketHeader header;
    memset(&header, 0, sizeof(PacketHeader));
    header.magic = 0xABCD;
    header.opcode = Opcode::DELETE_FILE;
    header.filename_len = (uint8_t)filename.length();
    loadSessionToken(header); 
    
    send(sock_fd, &header, sizeof(PacketHeader), 0);
    send(sock_fd, filename.c_str(), filename.length(), 0);

    PacketHeader ack;
    if (recv(sock_fd, &ack, sizeof(PacketHeader), 0) > 0 && ack.opcode == Opcode::ACK) {
        if (ack.payload_size == 1) std::cout << "[+] Deleted " << filename << " successfully.\n";
        else std::cerr << "[-] Failed to delete (Access denied or file missing).\n";
    }
    
    disconnect();
    return true;
}

bool NetworkClient::renameRemoteFile(const std::string& oldName, const std::string& newName) {
    if (!connectToServer()) return false;

    std::string payload = oldName + "|" + newName;
    PacketHeader header;
    memset(&header, 0, sizeof(PacketHeader));
    header.magic = 0xABCD;
    header.opcode = Opcode::RENAME_FILE;
    header.filename_len = (uint8_t)payload.length();
    loadSessionToken(header); 
    
    send(sock_fd, &header, sizeof(PacketHeader), 0);
    send(sock_fd, payload.c_str(), payload.length(), 0);

    PacketHeader ack;
    if (recv(sock_fd, &ack, sizeof(PacketHeader), 0) > 0 && ack.opcode == Opcode::ACK) {
        if (ack.payload_size == 1) std::cout << "[+] Renamed to " << newName << " successfully.\n";
        else std::cerr << "[-] Failed to rename (Access denied or file missing).\n";
    }
    
    disconnect();
    return true;
}
bool NetworkClient::getFileInfo(const std::string& filename) {
    if (!connectToServer()) return false;

    PacketHeader header;
    memset(&header, 0, sizeof(PacketHeader));
    header.magic = 0xABCD;
    header.opcode = Opcode::FILE_INFO;
    header.filename_len = (uint8_t)filename.length();
    loadSessionToken(header); 

    send(sock_fd, &header, sizeof(PacketHeader), 0);
    send(sock_fd, filename.c_str(), filename.length(), 0);

    PacketHeader ack_header;
    if (recv(sock_fd, &ack_header, sizeof(PacketHeader), 0) > 0 && ack_header.opcode == Opcode::ACK) {
        uint32_t incoming_size = ack_header.payload_size;
        
        if (incoming_size > 0) {
            std::vector<char> buffer(incoming_size + 1, '\0');
            uint32_t total_received = 0;
            while (total_received < incoming_size) {
                int chunk = recv(sock_fd, buffer.data() + total_received, incoming_size - total_received, 0);
                if (chunk <= 0) break;
                total_received += chunk;
            }
            
            std::cout << "\n" << buffer.data() << "\n";
        }
    } else {
        std::cerr << "[-] Failed to retrieve file info.\n";
    }

    disconnect();
    return true;
}
bool NetworkClient::createDirectory(const std::string& dirname) {
    if (!connectToServer()) return false;

    PacketHeader header;
    memset(&header, 0, sizeof(PacketHeader));
    header.magic = 0xABCD;
    header.opcode = Opcode::CREATE_DIR;
    header.filename_len = (uint8_t)dirname.length();
    loadSessionToken(header);

    send(sock_fd, &header, sizeof(PacketHeader), 0);
    send(sock_fd, dirname.c_str(), dirname.length(), 0);

    PacketHeader ack;
    if (recv(sock_fd, &ack, sizeof(PacketHeader), 0) > 0 && ack.opcode == Opcode::ACK) {
        if (ack.payload_size == 1) std::cout << "[+] Directory '" << dirname << "' created successfully.\n";
        else std::cerr << "[-] Failed to create directory (It may already exist or access denied).\n";
    }
    
    disconnect();
    return true;
}

bool NetworkClient::removeDirectory(const std::string& dirname) {
    if (!connectToServer()) return false;

    PacketHeader header;
    memset(&header, 0, sizeof(PacketHeader));
    header.magic = 0xABCD;
    header.opcode = Opcode::REMOVE_DIR;
    header.filename_len = (uint8_t)dirname.length();
    loadSessionToken(header);

    send(sock_fd, &header, sizeof(PacketHeader), 0);
    send(sock_fd, dirname.c_str(), dirname.length(), 0);

    PacketHeader ack;
    if (recv(sock_fd, &ack, sizeof(PacketHeader), 0) > 0 && ack.opcode == Opcode::ACK) {
        if (ack.payload_size == 1) std::cout << "[+] Directory '" << dirname << "' removed successfully.\n";
        else std::cerr << "[-] Failed to remove directory (It must be empty or may not exist).\n";
    }
    
    disconnect();
    return true;
}

bool NetworkClient::searchFiles(const std::string& query) {
    if (!connectToServer()) return false;

    PacketHeader header;
    memset(&header, 0, sizeof(PacketHeader));
    header.magic = 0xABCD;
    header.opcode = Opcode::SEARCH;
    header.filename_len = (uint8_t)query.length();
    loadSessionToken(header);

    send(sock_fd, &header, sizeof(PacketHeader), 0);
    send(sock_fd, query.c_str(), query.length(), 0);

    PacketHeader ack_header;
    if (recv(sock_fd, &ack_header, sizeof(PacketHeader), 0) > 0 && ack_header.opcode == Opcode::ACK) {
        uint32_t incoming_size = ack_header.payload_size;
        
        if (incoming_size > 0) {
            std::vector<char> buffer(incoming_size + 1, '\0');
            uint32_t total_received = 0;
            
            while (total_received < incoming_size) {
                int chunk = recv(sock_fd, buffer.data() + total_received, incoming_size - total_received, 0);
                if (chunk <= 0) break;
                total_received += chunk;
            }
            
            std::cout << "\n[+] Search Results:\n" << buffer.data() << "\n";
        }
    } else {
        std::cerr << "[-] Server failed to respond to SEARCH command.\n";
    }

    disconnect();
    return true;
}

bool NetworkClient::getHistory() {
    if (!connectToServer()) return false;

    PacketHeader header;
    memset(&header, 0, sizeof(PacketHeader));
    header.magic = 0xABCD;
    header.opcode = Opcode::HISTORY;
    loadSessionToken(header);

    send(sock_fd, &header, sizeof(PacketHeader), 0);

    PacketHeader ack;
    if (recv(sock_fd, &ack, sizeof(PacketHeader), 0) > 0 && ack.opcode == Opcode::ACK) {
        uint32_t incoming_size = ack.payload_size;
        
        if (incoming_size > 0) {
            std::vector<char> buffer(incoming_size + 1, '\0');
            uint32_t total_received = 0;
            
            while (total_received < incoming_size) {
                int chunk = recv(sock_fd, buffer.data() + total_received, incoming_size - total_received, 0);
                if (chunk <= 0) break;
                total_received += chunk;
            }
            std::cout << "\n" << buffer.data() << "\n";
        }
    }
    
    disconnect();
    return true;
}
