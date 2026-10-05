#ifndef NET_IO_H
#define NET_IO_H

#include <string>
#include <stddef.h>
#include <stdint.h>
#include "protocol.h"

// Read exactly n bytes (loops over short reads, retries on EINTR).
bool readExact(int fd, void* buf, size_t n);

// Write exactly n bytes (loops over short writes, MSG_NOSIGNAL so a dropped
// peer returns false instead of killing the process with SIGPIPE).
bool writeAll(int fd, const void* buf, size_t n);

// Header helpers: convert to/from network byte order and validate the magic.
bool sendHeader(int fd, uint8_t opcode, uint8_t filename_len, uint32_t payload_size,
                const std::string& token = std::string());
bool recvHeader(int fd, PacketHeader& header);

// Reply helpers (server side).
bool sendOk(int fd);                                  // ACK, no body
bool sendAckData(int fd, const std::string& data);    // ACK + body
bool sendError(int fd, const std::string& message);   // ERROR_REPLY + message

void setSocketTimeouts(int fd, int seconds);

#endif
