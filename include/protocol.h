#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

// Wire protocol
// -------------
// Every message starts with a fixed 40-byte PacketHeader. Multi-byte fields
// (magic, payload_size) travel in NETWORK byte order; use sendHeader() /
// recvHeader() from net_io.h, which do the conversion.
//
// Requests:
//   header [+ filename_len bytes of "name"] [+ payload_size bytes of body]
//   UPLOAD   : name = file name, payload_size = file size, body = file bytes
//   AUTH     : name empty, payload_size = sizeof(AuthPayload), body = AuthPayload
//   RENAME   : name = "old|new"
//   others   : name = file/dir name or search query (may be empty for LIST/HISTORY/LOGOUT)
//   Every request except AUTH must carry the session token in the header.
//
// Replies:
//   ACK          : payload_size = number of body bytes that follow (0 = plain "OK")
//   ERROR_REPLY  : payload_size = length of a human-readable message that follows
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
    LOGOUT = 14
};

constexpr uint16_t PROTOCOL_MAGIC = 0xABCD;
constexpr size_t   SESSION_TOKEN_LEN = 32;               // 32 hex chars (128 bits), no NUL terminator
constexpr uint32_t MAX_UPLOAD_SIZE = 256u * 1024u * 1024u; // 256 MiB per file
constexpr uint32_t MAX_META_PAYLOAD = 1u << 20;          // cap for text replies (1 MiB)

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
