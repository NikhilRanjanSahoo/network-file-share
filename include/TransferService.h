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

    // Receives exactly payload_size bytes into a temp file and atomically renames it
    // to STORAGE_ROOT/<filename> on success. `filename` must already be validated.
    // Sends NO reply: the caller decides what to tell the client.
    bool receiveFile(int socket_fd, const std::string& filename, uint32_t payload_size);

    // Streams a regular file as ACK(size) + bytes. On failure sends an ERROR_REPLY
    // (if nothing was sent yet) and returns false. out_sent gets the file size.
    bool sendFile(int socket_fd, const std::string& filepath, uint32_t* out_sent = nullptr);
};

#endif
