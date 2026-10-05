#ifndef PERMISSIONSERVICE_H
#define PERMISSIONSERVICE_H

#include <string>

// Single source of truth for role-based access. The server calls these on
// every request; the client UI only mirrors them for convenience.
class PermissionService {
public:
    static bool isKnownRole(const std::string& role);

    static bool canBrowse(const std::string& role);      // list / info / search
    static bool canUpload(const std::string& role);
    static bool canDownload(const std::string& role);
    static bool canDelete(const std::string& role);
    static bool canRename(const std::string& role);
    static bool canManageDirs(const std::string& role);  // mkdir / rmdir
    static bool canViewHistory(const std::string& role);
};

#endif
