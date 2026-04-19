#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <errno.h>
#include <iostream>
#include <cstring>
#include <sstream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <unordered_map>

#include "defRoutes.h"
#include "router.h"

#define BACKLOG 10
#define BUFFERSIZE 30000 

using namespace std;

string getMethod(char buffer[BUFFERSIZE]);
string getPath(char buffer[BUFFERSIZE]);
string getClientBody(char buffer[BUFFERSIZE]);
string getFileExt(string path);
int getFileDesc(string path);
bool sendResponse(int conn_fd, string& path, string& response);

string headerString = "HTTP/1.1";

std::unordered_map<std::string, std::string> mimetype = {
    {"html", "text/html"},
    {"htm",  "text/html"},
    {"css",  "text/css"},
    {"js",   "text/javascript"},
    {"json", "application/json"},
    {"jpg",  "image/jpeg"},
    {"jpeg", "image/jpeg"},
    {"png",  "image/png"},
    {"gif",  "image/gif"},
    {"svg",  "image/svg+xml"},
    {"webp", "image/webp"},
    {"ico",  "image/vnd.microsoft.icon"},
    {"pdf",  "application/pdf"},
    {"mp4",  "video/mp4"},
    {"mp3",  "audio/mpeg"},
    {"zip",  "application/zip"},
    {"txt",  "text/plain"},
    {"csv",  "text/csv"},
    {"xml",  "application/xml"},
    {"woff2","font/woff2"}
};

int main (int argc, char* argv[]) {

    if (argc != 3) {
        cout <<"Error: Takes two arguments: host and port"<<endl;
        return 1; 
    } // do some input checking

    int sock_fd;
    struct addrinfo hints;
    struct addrinfo* result;
    struct sockaddr_storage conn_addr;

    memset(&hints, 0, sizeof(hints));
    
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    int status = getaddrinfo(argv[1], argv[2], &hints, &result);
    if (status != 0) {
        cerr << "getaddrinfo: " << gai_strerror(status) << endl;
        exit(EXIT_FAILURE);
    }


    for (addrinfo* p = result; p != nullptr; p = p->ai_next) {
        sockaddr_in* ipv4 = (sockaddr_in*)p->ai_addr; 
        char ipstr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ipv4->sin_addr, ipstr, sizeof(ipstr));

        sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock_fd == -1) {
            cerr << "socket() failed, trying next addrinfo..." << endl;
            continue;
        }

        cout << "Socket created successfully on address: " << ipstr << endl;
        break;
    }


    int yes = 1;
    int setsockstatus = setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (setsockstatus != 0) {
        cerr << "setsockopt() failed: " << gai_strerror(setsockstatus);
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    int bindstatus = bind(sock_fd, result->ai_addr, result->ai_addrlen);
    if (bindstatus != 0) {
        cerr<< "bind() failed: " << gai_strerror(bindstatus) <<endl;
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    int lsnstatus = listen(sock_fd, BACKLOG);
    if (lsnstatus != 0) {
        cerr << "listen() failed: " << gai_strerror(lsnstatus) <<endl;
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    std::cout << "Listening on address: " << argv[1] << ":" <<argv[2] <<endl;

    Router GET = usrts::constructRoutes("GET");
    Router POST = usrts::constructRoutes("POST");
    Router PATCH = usrts::constructRoutes("PATCH");
    Router PUT = usrts::constructRoutes("PUT");
    Router DELETE = usrts::constructRoutes("DELETE");

    while (true) {
        socklen_t addr_len = sizeof(conn_addr);
        int conn_sock_fd = accept(sock_fd, (struct sockaddr*)&conn_addr, &addr_len);
        if (conn_sock_fd == -1) {
            cerr << "listen() failed: " << gai_strerror(lsnstatus) <<endl;
            close(sock_fd);
            close(conn_sock_fd);
            exit(EXIT_FAILURE);
        }

        int connection_pid = fork();
        if(connection_pid < 0){
            cerr << "Error on fork" << endl;
            exit(EXIT_FAILURE);
        }
        if (connection_pid == 0) {
            char buffer[BUFFERSIZE] = {0};
            read(conn_sock_fd, buffer, BUFFERSIZE);
            cout<<"Buffer read: "<<buffer<<endl;

            string client_method = getMethod(buffer);
            string client_req_path = "." + getPath(buffer);
            string client_file_ext = getFileExt(client_req_path);
            string client_body = getClientBody(buffer);

            if (client_method == "GET") {
                string response_string = headerString;
                int file_fd;
                if (file_fd = getFileDesc(client_req_path); file_fd > 0) {
                    response_string = response_string + " 200\r\n"; 
                    
                    struct stat file_buf{};
                    if (fstat(file_fd, &file_buf) < 0) {
                        cerr << "Error fstat file" <<endl;
                        close(file_fd);
                    }
                    if (S_ISDIR(file_buf.st_mode)) {
                        std::cout << "====DIR====";
                        client_req_path = client_req_path + ((response_string.back() == '/')?"":"/") + "index.html";
                        client_file_ext = "html";
                    }
                    response_string = response_string + "Content-Type: " + mimetype[client_file_ext] + "\r\n";
                    close(file_fd);

                    sendResponse(conn_sock_fd, client_req_path, response_string);
                    close(conn_sock_fd);
                    exit(0);
                }
                else {
                    response_string += " 404\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                    write(conn_sock_fd, response_string.c_str(), response_string.size());
                    close(conn_sock_fd);
                    exit(0);
                }
            }
            else if (client_method == "POST") {
                string message = POST.callRoute(client_req_path.substr(1), client_body);
                string response_string = headerString;
                response_string = response_string + " 200\r\nContent-Length: " + to_string(message.size()) + "\r\nConnection: close\r\n\r\n"; 
                response_string = response_string + message;
                std::cout << "method is: "<< client_method <<endl;
                std::cout << "path is: "<< client_req_path.substr(1) <<endl;
                std::cout << "message is: "<< message <<endl;
                std::cout << "response is: "<< response_string <<endl;
                write(conn_sock_fd, response_string.c_str(), response_string.size());
                close(conn_sock_fd);
                exit(0);
            }
            else if (client_method == "DELETE") {
                string message = DELETE.callRoute(client_req_path, client_body);
            }
            else if (client_method == "PATCH") {
                string message = PATCH.callRoute(client_req_path, client_body);
            }
            else if (client_method == "PUT") {
                string message = PUT.callRoute(client_req_path, client_body);
            }
        }
        
    }

    return 0;
}

string getMethod(char buffer[BUFFERSIZE]) {
    istringstream iss(buffer);

    string token;
    iss >> token;

    return token;
}

string getPath(char buffer[BUFFERSIZE]) {
    istringstream iss(buffer);

    string token;
    iss >> token;
    iss >> token;

    return token;
}

string getFileExt(string path) {
    size_t pos = path.rfind('.');
    if (pos == std::string::npos){
        return "";
    }
    return path.substr(pos + 1); 
}

int getFileDesc(string path) {
    int file_fd = open(path.c_str(), O_RDONLY);
    if (file_fd < 0) {
        cerr << "Error opening file" << path <<endl;
        close(file_fd);
        return -1;
    }

    struct stat file_buf{};
    if (fstat(file_fd, &file_buf) < 0) {
        cerr << "Error fstat file" <<endl;
        close(file_fd);
        return -1;
    }
    return file_fd;
}

bool sendResponse(int conn_fd, string& path, string& response) {
    int file_fd = getFileDesc(path);

    struct stat file_buf{};
    if (fstat(file_fd, &file_buf) < 0) {
        cerr << "Error fstat file" <<endl;
        close(file_fd);
    }

    size_t remaining = file_buf.st_size;
    size_t block_size = file_buf.st_blksize;

    response = response + "Content-Length: " + to_string(remaining) + "\r\n";
    response = response + "Connection: close\r\n\r\n";

    ssize_t header_written = write(conn_fd, response.c_str(), response.size());
    if (header_written < 0) {
        cerr << "Error writing file" <<endl;
        return false;
    }

    while (remaining > 0) {
        ssize_t sent = sendfile(conn_fd, file_fd, NULL, block_size);
        if (sent <= 0) {
            close(file_fd);
            return false;
        }
        remaining -= sent;
    }

    close(file_fd);
    return true;
}
string getClientBody(char buffer[BUFFERSIZE]) {
    string request(buffer);

    string breaker = "\r\n\r\n";
    size_t pos = request.find(breaker);

    if (pos == string::npos) {
        return ""; // no body found
    }

    return request.substr(pos + breaker.length());
}
