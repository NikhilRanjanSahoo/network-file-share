#include "../include/PermissionService.h"

namespace {
bool isStaff(const std::string& role) {
    return role == "Admin" || role == "Faculty";
}
}

bool PermissionService::isKnownRole(const std::string& role) {
    return role == "Admin" || role == "Faculty" || role == "Student";
}

bool PermissionService::canBrowse(const std::string& role)   { return isKnownRole(role); }
bool PermissionService::canUpload(const std::string& role)   { return isKnownRole(role); }
bool PermissionService::canDownload(const std::string& role) { return isKnownRole(role); }

bool PermissionService::canDelete(const std::string& role)      { return isStaff(role); }
bool PermissionService::canRename(const std::string& role)      { return isStaff(role); }
bool PermissionService::canManageDirs(const std::string& role)  { return isStaff(role); }
bool PermissionService::canViewHistory(const std::string& role) { return isStaff(role); }
