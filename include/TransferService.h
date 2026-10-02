#ifndef TRANSFERSERVICE_H
#define TRANSFERSERVICE_H

#include <string>
#include <stdint.h>
#include "FileManager.h"

class TransferService {
private:
    FileManager fileManager;

public:
    TransferService();
    ~TransferService();

    
    bool sendFile(int socket_fd, const std::string& filepath);
    bool receiveFile(int socket_fd, const std::string& filename, uint32_t payload_size);
    
    
    std::string calculateHash(const std::string& filepath);
};

#endif 
