#ifndef PERMISSIONSERVICE_H
#define PERMISSIONSERVICE_H

#include <string>

class PermissionService {
public:
    
    static bool canDelete(const std::string& role);
    static bool canRename(const std::string& role);
    static bool canUpload(const std::string& role);
    static bool canDownload(const std::string& role);
};

#endif 
