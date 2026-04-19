#include "defRoutes.h"

#include <string>
#include <sstream>
#include "router.h"

namespace usrts {
    
    // USER FUNCTIONS GO HERE
    std::string testFunc(const std::string& value) {
        return value + " hello, world!";
    }

    std::string submitFunc(const std::string& value) {
        std::stringstream ss(value);
        std::string token;

        std::string username, age;

        while (getline(ss, token, '&')) {
            size_t pos = token.find('=');
            std::string key = token.substr(0, pos);
            std::string value = token.substr(pos + 1);

            if (key == "username") username = value;
            if (key == "age") age = value;
        }
        return "Hello "+username+"! "+"Your age is: "+age;
    }
    // ROUTES GO HERE
    Router constructRoutes(std::string method) {
        if (method == "GET") {
            Router GET;
            
            return GET;
        }
        else if (method == "POST") {
            Router POST;
            POST.makeRoute("/test", testFunc);
            POST.makeRoute("/submit", submitFunc);
            
            return POST;
        }
        else if (method == "UPDATE") {
            Router UPDATE;
            
            return UPDATE;
        }
        else if (method == "PUT") {
            Router PUT;
            
            return PUT;
        }
        else if (method == "DELETE") {
            Router DELETE;
            
            return DELETE; 
        }
        else {
            Router CUSTOM;
            return CUSTOM;
        }

    }

}
