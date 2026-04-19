#include "router.h"

#include <string>
#include <functional>
#include <unordered_map>

void Router::makeRoute(const std::string& route_path, std::function<std::string(const std::string&)> route_func) {
    routes[route_path] = route_func;
}

std::string Router::callRoute(const std::string& route_path, const std::string& client_body) {
    if (routes.find(route_path) != routes.end()) {
        return routes[route_path](client_body);
    }
    return "";
}
