#include "../include/NetworkClient.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 4) {
        std::cerr << "Usage: ./client <upload|download> <src_filepath> [dst_filepath]\n";
        return 1;
    }

    std::string action = argv[1];
    std::string filepath = argv[2];
    std::string dst_filepath = (argc == 4) ? argv[3] : "";

    NetworkClient client("127.0.0.1", 8080);

    if (action == "upload") {
        client.upload(filepath);
    } else if (action == "download") {
        
        client.download(filepath, dst_filepath);
    } else {
        std::cerr << "[-] Unknown command. Use 'upload' or 'download'.\n";
    }

    return 0;
}
