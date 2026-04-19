#ifndef ROUTER_H
#define ROUTER_H

#include <string>
#include <functional>
#include <unordered_map>

class Router {
public:
    void makeRoute(const std::string& route_path, std::function<std::string(const std::string&)> route_func);
    std::string callRoute(const std::string& route_path, const std::string& client_body);

private:
    std::unordered_map<std::string, std::function<std::string(const std::string&)>> routes;
};

#endif // ROUTER_H
