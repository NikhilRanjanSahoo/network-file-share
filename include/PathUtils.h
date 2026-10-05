#ifndef PATHUTILS_H
#define PATHUTILS_H

#include <string>

inline bool isSafeName(const std::string& name) {
    if (name.empty() || name.size() > 255) return false;
    if (name == "." || name == "..") return false;
    for (unsigned char c : name) {
        if (c < 0x20 || c == 0x7F) return false;
        if (c == '/' || c == '\\' || c == '|') return false;
    }
    return true;
}

inline std::string baseName(const std::string& path) {
    const size_t pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

#endif
