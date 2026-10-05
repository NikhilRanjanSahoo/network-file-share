#include "../include/net_io.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>

bool readExact(int fd, void* buf, size_t n) {
    char* p = static_cast<char*>(buf);
    while (n > 0) {
        ssize_t r = recv(fd, p, n, 0);
        if (r > 0) {
            p += r;
            n -= static_cast<size_t>(r);
        } else if (r < 0 && errno == EINTR) {
            continue;
        } else {
            return false;   // closed, timed out (EAGAIN) or error
        }
    }
    return true;
}

bool writeAll(int fd, const void* buf, size_t n) {
    const char* p = static_cast<const char*>(buf);
    while (n > 0) {
        ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
        if (w > 0) {
            p += w;
            n -= static_cast<size_t>(w);
        } else if (w < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

bool sendHeader(int fd, uint8_t opcode, uint8_t filename_len, uint32_t payload_size,
                const std::string& token) {
    PacketHeader h;
    std::memset(&h, 0, sizeof(h));
    h.magic = htons(PROTOCOL_MAGIC);
    h.opcode = opcode;
    h.filename_len = filename_len;
    h.payload_size = htonl(payload_size);
    std::memcpy(h.session_token, token.data(), std::min(token.size(), SESSION_TOKEN_LEN));
    return writeAll(fd, &h, sizeof(h));
}

bool recvHeader(int fd, PacketHeader& header) {
    if (!readExact(fd, &header, sizeof(header))) return false;
    header.magic = ntohs(header.magic);
    header.payload_size = ntohl(header.payload_size);
    return header.magic == PROTOCOL_MAGIC;
}

bool sendOk(int fd) {
    return sendHeader(fd, Opcode::ACK, 0, 0);
}

bool sendAckData(int fd, const std::string& data) {
    return sendHeader(fd, Opcode::ACK, 0, static_cast<uint32_t>(data.size())) &&
           writeAll(fd, data.data(), data.size());
}

bool sendError(int fd, const std::string& message) {
    std::string msg = message.substr(0, 4096);
    return sendHeader(fd, Opcode::ERROR_REPLY, 0, static_cast<uint32_t>(msg.size())) &&
           writeAll(fd, msg.data(), msg.size());
}

void setSocketTimeouts(int fd, int seconds) {
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}
