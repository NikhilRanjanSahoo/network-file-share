#ifndef USER_H
#define USER_H

#include <string>

class User {
protected:
    int id;
    std::string username;
    std::string password;
    std::string role;

public:
    User(int id, const std::string& user, const std::string& pass, const std::string& role) 
        : id(id), username(user), password(pass), role(role) {}
    
    virtual ~User() = default;

    virtual bool canDelete() const = 0; 
    
    std::string getRole() const { return role; }
};

class Admin : public User {
public:
    Admin(int id, const std::string& user, const std::string& pass) 
        : User(id, user, pass, "Admin") {}
    
    bool canDelete() const override { return true; } 
};

class Faculty : public User {
public:
    Faculty(int id, const std::string& user, const std::string& pass) 
        : User(id, user, pass, "Faculty") {}
    
    bool canDelete() const override { return true; } 
};

class Student : public User {
public:
    Student(int id, const std::string& user, const std::string& pass) 
        : User(id, user, pass, "Student") {}
    
    bool canDelete() const override { return false; } 
};

#endif
