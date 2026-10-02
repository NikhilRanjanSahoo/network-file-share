#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

enum Opcode : uint8_t {
    UPLOAD = 1,
    DOWNLOAD = 2,
    ACK = 3,
    AUTH = 4,
    LIST=5
};

#pragma pack(push, 1)
struct PacketHeader {
    uint16_t magic;         
    uint8_t opcode;         
    uint8_t filename_len;   
    uint32_t payload_size;  
};                          


struct AuthPayload {
    char username[32];
    char password[32];
};
#pragma pack(pop)

#endif 
