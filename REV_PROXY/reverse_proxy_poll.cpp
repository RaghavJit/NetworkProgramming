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
#include <poll.h>
#include <signal.h>
#include <fcntl.h>

#define BACKLOG 10
#define BUFFER_SIZE 3000 

using namespace std;

static const size_t SPL = 65536;

void proxy_with_splice(int client_fd, int backend_fd) {
    int c2b_pipe[2];
    int b2c_pipe[2];

    if (pipe(c2b_pipe) < 0 || pipe(b2c_pipe) < 0) {
        perror("pipe");
        return;
    }

    struct pollfd fds[2];
    fds[0].fd = client_fd;
    fds[0].events = POLLIN;

    fds[1].fd = backend_fd;
    fds[1].events = POLLIN;

    for (;;) {
        int r = poll(fds, 2, -1);
        if (r < 0) {
            perror("poll");
            break;
        }

        if (fds[0].revents & POLLIN) {
            ssize_t n = splice(client_fd, NULL, c2b_pipe[1], NULL, SPL,SPLICE_F_MOVE | SPLICE_F_MORE);
            if (n <= 0) break;

            ssize_t m = splice(c2b_pipe[0], NULL, backend_fd, NULL, n, SPLICE_F_MOVE | SPLICE_F_MORE);
            if (m <= 0) break;
        }

        if (fds[1].revents & POLLIN) {
            ssize_t n = splice(backend_fd, NULL, b2c_pipe[1], NULL, SPL, SPLICE_F_MOVE | SPLICE_F_MORE);
            if (n <= 0) break;

            ssize_t m = splice(b2c_pipe[0], NULL, client_fd, NULL, n, SPLICE_F_MOVE | SPLICE_F_MORE);
            if (m <= 0) break;
        }
    }

    close(c2b_pipe[0]);
    close(c2b_pipe[1]);
    close(b2c_pipe[0]);
    close(b2c_pipe[1]);
}

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

    int fe_sock_fd, be_sock_fd;
    
    addrinfo* result = createSocket(fe_sock_fd, frontend_host, frontend_port);

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
    while (true) {
        cout<< "Entered Loop" << endl;
        socklen_t addr_len = sizeof(conn_addr);
        cout<< "Accepted" << endl;
        int conn_sock_fd = accept(fe_sock_fd, (struct sockaddr*)&conn_addr, &addr_len);
        if (conn_sock_fd == -1) {
            cerr << "Error at accept()"<<endl;
            close(fe_sock_fd);
            close(conn_sock_fd);
            continue;
        }
        cout<< "Client accepted" << endl;

        int connection_pid = fork();
        if(connection_pid < 0){
            cerr << "Error on fork" << endl;
            exit(EXIT_FAILURE);
        }
        if (connection_pid == 0) {
            addrinfo* backend_result = createSocket(be_sock_fd, backend_host, backend_port);
            int conn_status = connect(be_sock_fd, backend_result->ai_addr, backend_result->ai_addrlen);
            if (conn_status < 0) {
                cerr << "Error at backend connect(): " << gai_strerror(conn_status);
                close(be_sock_fd);
                close(fe_sock_fd);
                return -1;
            }

            proxy_with_splice(conn_sock_fd, be_sock_fd);

            close(conn_sock_fd);
            close(be_sock_fd);
            close(fe_sock_fd);
            exit(0);
        } 
    }
    return 0;
}


