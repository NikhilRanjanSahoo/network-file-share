#include "../include/PermissionService.h"

bool PermissionService::canDelete(const std::string& role) {
    
    return (role == "Admin" || role == "Faculty");
}

bool PermissionService::canRename(const std::string& role) {
    
    return (role == "Admin" || role == "Faculty");
}

bool PermissionService::canUpload(const std::string& role) {
    return true; 
}

bool PermissionService::canDownload(const std::string& role) {
    return true; 
}
