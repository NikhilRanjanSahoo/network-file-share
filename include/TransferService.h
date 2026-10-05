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

    bool receiveFile(int socket_fd, const std::string& destPath, uint32_t payload_size);
    bool sendFile(int socket_fd, const std::string& filepath, uint32_t* out_sent = nullptr);
};

#endif
