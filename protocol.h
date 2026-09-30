#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>


enum class Opcode : uint8_t {
    UPLOAD = 0x01,
    DOWNLOAD = 0x02,
    LIST = 0x03,
    DELETE = 0x04,
    ACK = 0x05
};


struct __attribute__((packed)) PacketHeader {
    uint16_t magic = 0xABCD; 
    Opcode opcode;           
    uint8_t reserved = 0x00; 
    uint32_t payload_size;   
};

#endif
