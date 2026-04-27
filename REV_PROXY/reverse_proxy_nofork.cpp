#include <asm-generic/socket.h>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/select.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <signal.h>
#include <fcntl.h>

#define BACKLOG 10
#define BUFFER_SIZE 3000 
#define WORKERS 1 

using namespace std;


addrinfo* createSocket(int &sock_fd, string host, string port) {
    struct addrinfo hints{};
    struct addrinfo* result;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;

    int status = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
    if (status != 0) {
        cerr << "getaddrinfo: " << gai_strerror(status) << endl;
        return nullptr;
    }

    for (addrinfo* p = result; p != nullptr; p = p->ai_next) {
        int fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;

        sock_fd = fd;
        return p;                 // RETURN THE MATCHING ENTRY
    }

    return nullptr;
}
int main (int argc, char *argv[]) { // input checking
    
    signal(SIGPIPE, SIG_IGN);
    string backend_host = argv[1];
    string backend_port= argv[2];
    string frontend_host = argv[3];
    string frontend_port = argv[4];

    int fe_sock_fd;
    
    addrinfo* result = createSocket(fe_sock_fd, frontend_host, frontend_port);
    // fcntl(fe_sock_fd, F_SETFL, O_NONBLOCK);

    int yes = 1;
    int setsock_status = setsockopt(fe_sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (setsock_status != 0) {
        cerr << "Error at setsockopt(): " << gai_strerror(setsock_status) << endl;
        return -1;
    }

    int bind_status = bind(fe_sock_fd, result->ai_addr, result->ai_addrlen);
    if (bind_status != 0) {
        cerr << "Error at bind(): " << gai_strerror(bind_status) << endl;
        close(fe_sock_fd);
        return -1;
    }


    int lsn_status = listen(fe_sock_fd, BACKLOG);
    if (lsn_status != 0) {
        cerr<< "Error at listen(): " << gai_strerror(lsn_status) << endl;
        close(fe_sock_fd);
        return -1;
    }
    std::cout<< "Listening to clients" <<endl;

    struct sockaddr_storage conn_addr;
    
    int epoll_fd;
    epoll_event be_epoll_event;
    for (int i=0; i<WORKERS; i++) {
        int connection_pid = fork();
        if(connection_pid < 0){
            cerr << "Error on fork" << endl;
            exit(EXIT_FAILURE);
        }
    }

    while (true) {

        struct sockaddr_storage conn_addr;
        socklen_t addrlen = sizeof(conn_addr);
        int conn_sock_fd = accept(fe_sock_fd, (struct sockaddr*)&conn_addr, &addrlen); 
        if (conn_sock_fd == -1) {
            close(conn_sock_fd);
            continue;
        }
        int be_sock_fd;
        addrinfo* be_result = createSocket(be_sock_fd, backend_host, backend_port); 
        int conn_status = connect(be_sock_fd, be_result->ai_addr, be_result->ai_addrlen);
        if (conn_status < 0) {
            cerr << "Error at backend connect(): " << gai_strerror(conn_status);
            close(be_sock_fd);
            close(conn_sock_fd);
            break;
        }

        while (true) {
        
            // select logic
            fd_set readfds, writefds;
            int nfds = (be_sock_fd > conn_sock_fd ? be_sock_fd : conn_sock_fd) + 1;

            FD_ZERO(&readfds);
            FD_ZERO(&writefds);
            FD_SET(be_sock_fd, &readfds);
            FD_SET(conn_sock_fd, &readfds);
            
            int ready = select(nfds, &readfds, NULL, NULL, NULL);
            if (ready < 0) {
                cerr << "Error select(): " << gai_strerror(ready);  
            }

            ssize_t chunk;
            char c2b_buffer[BUFFER_SIZE];
            char b2c_buffer[BUFFER_SIZE];
            if (FD_ISSET(conn_sock_fd, &readfds)) {
                chunk = recv(conn_sock_fd, c2b_buffer, BUFFER_SIZE, MSG_DONTWAIT);
                if (chunk <= 0) {
                    close(conn_sock_fd);
                    close(be_sock_fd);
                    break;
                }
                send(be_sock_fd, c2b_buffer, chunk, MSG_DONTWAIT);
            }
            if (FD_ISSET(be_sock_fd, &readfds)) {
                chunk = recv(be_sock_fd, b2c_buffer, BUFFER_SIZE, MSG_DONTWAIT);
                send(conn_sock_fd, b2c_buffer, chunk, MSG_DONTWAIT);
                if (chunk <= 0) {
                    close(conn_sock_fd);
                    close(be_sock_fd);
                    break;
                }
            }
        }

        close(conn_sock_fd);
        close(be_sock_fd);
    }

    return 0;
}


