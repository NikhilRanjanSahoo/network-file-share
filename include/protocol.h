#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

enum Opcode : uint8_t {
    UPLOAD = 1,
    DOWNLOAD = 2,
    ACK = 3,
    AUTH = 4,
    LIST = 5,
    DELETE_FILE = 6,
    RENAME_FILE = 7,
    FILE_INFO = 8,
    CREATE_DIR = 9,
    REMOVE_DIR = 10,
    SEARCH = 11,
    HISTORY = 12,
    ERROR_REPLY = 13,
    LOGOUT = 14,
    ADD_USER = 15,
    REMOVE_USER = 16,
    SET_ROLE = 17,
    LIST_USERS = 18,
    SET_POLICY = 19,
    LIST_POLICY = 20,
    COPY_FILE = 21
};

constexpr uint16_t PROTOCOL_MAGIC = 0xABCD;
constexpr size_t   SESSION_TOKEN_LEN = 32;
constexpr uint32_t MAX_UPLOAD_SIZE = 256u * 1024u * 1024u;
constexpr uint32_t MAX_META_PAYLOAD = 1u << 20;

#pragma pack(push, 1)
struct PacketHeader {
    uint16_t magic;
    uint8_t  opcode;
    uint8_t  filename_len;
    uint32_t payload_size;
    char     session_token[SESSION_TOKEN_LEN];
};

struct AuthPayload {
    char username[32];
    char password[32];
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) == 40, "PacketHeader must be exactly 40 bytes on the wire");

#endif
