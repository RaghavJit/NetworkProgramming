#include <asm-generic/socket.h>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <openssl/buffer.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string>
#include <fcntl.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

using namespace std;

#define BUFFER_SIZE 2000

string constructQuery(string method, string host, string path, string content_type="", string range="") {
    string request;
    request  = method + " " + path + " HTTP/1.1\r\n";
    request += "Host: " + host + "\r\n";
    request += "User-Agent: dgmr/1.1.1\r\n";
    if (range != "") {
        request += "Range: " + range + "\r\n";
    }
    request += "Content-Type: " + content_type + "\r\n";
    request += "Connection: close\r\n\r\n";

    return request;
}

void init_openssl() {
    SSL_library_init();                  
    OpenSSL_add_all_algorithms();        
    SSL_load_error_strings();            
}

SSL_CTX* create_client_ssl_context() {
    const SSL_METHOD* method = TLS_client_method();
    SSL_CTX* ctx = SSL_CTX_new(method);
    if (!ctx) {
        perror("Client SSL context failed");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    return ctx;
}

int getContentLength(char buffer[BUFFER_SIZE], int bytes) {
    std::string response(buffer, bytes);

    std::string key = "Content-Length:";
    size_t pos = response.find(key);
    if (pos == std::string::npos) return -1;

    pos += key.length();

    while (pos < response.size() && std::isspace(response[pos])) {
        pos++;
    }

    size_t end = pos;
    while (end < response.size() && std::isdigit(response[end])) {
        end++;
    }

    if (end == pos) return -1;

    return std::stoi(response.substr(pos, end - pos));
}

string getContentType(char buffer[BUFFER_SIZE], int bytes) {
    std::string response(buffer, bytes);

    std::string key = "Content-Type:";
    size_t pos = response.find(key);
    if (pos == std::string::npos) return "*/*";

    pos += key.length();

    while (pos < response.size() && std::isspace(response[pos])) {
        pos++;
    }

    size_t end = pos;
    while (end < response.size() && (response[end]!='\r' && response[end+1]!='\n')) {
        end++;
    }

    if (end == pos) return "*/*";

    return response.substr(pos, end - pos);
}

bool stripHeader(char buffer[BUFFER_SIZE], size_t& pos) {
    string response(buffer, BUFFER_SIZE);

    string key = "\r\n\r\n";
    pos = response.find(key);

    if (pos==string::npos) {
        return false;
    }

    pos+=4;
    return true;
}

void sendSecQuery (int sock_fd, string request, size_t& content_len, string& content_type) {
    SSL_CTX* ctx = create_client_ssl_context();
    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock_fd); 

    if (SSL_connect(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
    } 
    else {
        SSL_write(ssl, request.c_str(), request.size());

        char buffer[BUFFER_SIZE];
        int bytes = SSL_read(ssl, buffer, BUFFER_SIZE);
        content_len = getContentLength(buffer, bytes);
        content_type = getContentType(buffer, bytes);
    }
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    return;
}

void sendQuery(int sock_fd, string request, size_t& content_len, string& content_type) {
    write(sock_fd, request.c_str(), request.size());

    char buffer[BUFFER_SIZE];
    int bytes = read(sock_fd, buffer, BUFFER_SIZE);

    content_len = getContentLength(buffer, bytes);
    content_type = getContentType(buffer, bytes);

    return;
}

int downloadFileSec(int sock_fd, int file_fd, size_t per_worker_load, ssize_t w_offset, string request) {
    SSL_CTX* ctx = create_client_ssl_context();
    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock_fd); 

    if (SSL_connect(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(file_fd);
        return -1;
    } 
    if (SSL_write(ssl, request.c_str(), request.size()) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(file_fd);
        return -1;
    }
    else {
        
        // header read loop
        size_t bytes_read;
        string header, body_start;
        char buffer[BUFFER_SIZE];
        while(true) {
            if (SSL_read_ex(ssl, buffer, BUFFER_SIZE, &bytes_read) <= 0) {
                ERR_print_errors_fp(stderr);
                SSL_free(ssl);
                SSL_CTX_free(ctx);
                close(file_fd);
                return -1;
            }
            string temp(buffer, bytes_read);
            header+=temp;
            if (size_t pos = header.find("\r\n\r\n"); pos != string::npos) {
                body_start = header.substr(pos + 4);
                cout<<"body"<<body_start<<endl;
                break;
            }

        }
        
        // body read loop
        pwrite(file_fd, body_start.data(), body_start.size(), w_offset);
        w_offset+=body_start.size();

        size_t remaining = per_worker_load;
        while(remaining > 0) {
            if (SSL_read_ex(ssl, buffer, BUFFER_SIZE, &bytes_read) <= 0) {
                ERR_print_errors_fp(stderr);
                SSL_free(ssl);
                SSL_CTX_free(ctx);
                return -1;
            }
            pwrite(file_fd, buffer, bytes_read, w_offset);
            w_offset+=bytes_read;
            remaining-=bytes_read;
        }

    }
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    return 0;
}

int downloadFile(int sock_fd, int file_fd, size_t per_worker_load, ssize_t w_offset, string request) {
    write(sock_fd, request.c_str(), request.size());

    struct stat file_buf{};
    if (fstat(file_fd, &file_buf) < 0) {
        cerr << "Error fstat file" <<endl;
        close(file_fd);
    }

    size_t remaining = per_worker_load;
    size_t block_size = file_buf.st_blksize;


    while (remaining > 0) {
        ssize_t sent = sendfile(file_fd, sock_fd, &w_offset, block_size);
        if (sent <= 0) {
            close(file_fd);
            return -1;
        }
        remaining -= sent;
    }
    return 0;
}

string getRange(int WORKER_INDEX, int workers, size_t content_length, size_t& w_offset, size_t& per_worker_load) {
    per_worker_load = content_length/workers;
    int start, end;
    
    start = per_worker_load*WORKER_INDEX;
    if (WORKER_INDEX+1==workers) {
        end = content_length-1;
        per_worker_load = end-start;
    }
    else {
        end = start+per_worker_load-1;
    }

    return "bytes=" + to_string(start) + "-" + to_string(end);
}

int getFileDesc(string path) {
    int file_fd = open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
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

int createConnectSocket(string host, string port){
    int sock_fd;
    struct addrinfo hints;
    struct addrinfo* result;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    int status = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
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

        result = p;
        break;
    }

    int conn_status = connect(sock_fd, result->ai_addr, result->ai_addrlen);
    if (conn_status < 0) {
        cerr << "Error at backend connect(): " << gai_strerror(conn_status);
        close(sock_fd);
        return -1;
    }
    return sock_fd;
}

int main (int argc, char *argv[]) {
    int workers = strtol(argv[1], NULL, 10);
    string link = argv[2];

    string host="";
    string port="";
    string path="";
    string file="";

    int mark ;
    port = ((mark = link.find("://")) == 5) ? "443" : "80";

    string trnc = link.substr(mark+3);
    int last = trnc.find("/");
    if (mark = trnc.find(":"); mark!=string::npos) {
        port = trnc.substr(mark+1, last-mark-1); 
        host = trnc.substr(0, mark);
    }
    else {
        host = trnc.substr(0, last);
    }
    path = trnc.substr(last);
    if (trnc.back()=='/') trnc.pop_back();
    file=trnc.substr(trnc.rfind("/")+1);

    int sock_fd = createConnectSocket(host, port);
    
    size_t content_length;
    string content_type;
    string len_query=constructQuery("HEAD", host, path);
    if (link[4]=='s') {
        sendSecQuery(sock_fd, constructQuery("HEAD", host, path), content_length, content_type);
        close(sock_fd);
    }
    else {
        sendQuery(sock_fd, constructQuery("HEAD", host, path), content_length, content_type);
        close(sock_fd);
    }

    for (int WORKER_INDEX=0; WORKER_INDEX<workers; WORKER_INDEX++) {
        sock_fd = createConnectSocket(host, port);
        int worker_pid = fork();
            if(worker_pid < 0){
            cerr << "Error on fork" << endl;
            exit(EXIT_FAILURE);
        }
        if (worker_pid == 0) {
            
            size_t per_worker_load, w_offset;
            string query = constructQuery("GET", host, path, content_type, getRange(WORKER_INDEX, workers, content_length, w_offset, per_worker_load));
            int file_fd = getFileDesc("./"+file);
            if (link[4]=='s') {
                downloadFileSec(sock_fd, file_fd, per_worker_load, w_offset, query);
            }
            else {
                downloadFile(sock_fd, file_fd, per_worker_load, w_offset, query);
            }

            close(file_fd);
            exit(0);
        }
    }

    close(sock_fd);
    return 0;
}
