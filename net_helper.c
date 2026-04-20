/*
    Chat App for Lab Final Project
    Modified net_helper code
    Modifications: Md. Shamsul Arefin
    BSSE: 1732
*/

// Define _POSIX_C_SOURCE for strdup
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

#define USERNAME_MAX 50 

typedef struct Connection {
    int fd;                      
    char peer_ip[INET6_ADDRSTRLEN];
    uint16_t peer_port;
    char username[USERNAME_MAX]; 
    int color_id; 
    
    //Add FILE streams 
    FILE *stream_read;
    FILE *stream_write;
    // ---
} Connection;

/* Create a listening socket. */
int listener_create(const char *ip, uint16_t port) {
    int srvfd = -1;
    int opt = 1;

    srvfd = socket(AF_INET, SOCK_STREAM, 0);
    if (srvfd < 0) return -1;

    if (setsockopt(srvfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(srvfd);
        return -1;
    }

    struct sockaddr_in addr4;
    memset(&addr4, 0, sizeof(addr4));
    addr4.sin_family = AF_INET;
    addr4.sin_port = htons(port);
    if (ip == NULL || strcmp(ip, "0.0.0.0") == 0) {
        addr4.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    else {
        if (inet_pton(AF_INET, ip, &addr4.sin_addr) != 1) {
            close(srvfd);
            errno = EINVAL;
            return -1;
        }
    }

    if (bind(srvfd, (struct sockaddr *)&addr4, sizeof(addr4)) < 0) {
        close(srvfd);
        return -1;
    }
    if (listen(srvfd, 10) < 0) {
        close(srvfd);
        return -1;
    }
    return srvfd;
}

/* Close a listening socket. */
void listener_close(int fd) {
    if (fd >= 0) close(fd);
}

/* Accept a new connection. */
Connection *listener_accept(int fd) {
    if (fd < 0) { errno = EBADF; return NULL; }

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int clifd = accept(fd, (struct sockaddr *)&addr, &len);
    if (clifd < 0) return NULL;

    Connection *c = malloc(sizeof(Connection));
    if (!c) {
        close(clifd);
        errno = ENOMEM;
        return NULL;
    }

    c->fd = clifd;
    inet_ntop(AF_INET, &addr.sin_addr, c->peer_ip, sizeof(c->peer_ip));
    c->peer_port = ntohs(addr.sin_port);
    c->color_id = 0; 
    c->stream_read = NULL;  
    c->stream_write = NULL; 
    
    return c;
}

/* Close a connection. */
void conn_close(Connection *c) {
    if (c) {
        // fd is closed by fclose on the streams
        // but we close it here just in case streams weren't dup'd
        if (c->fd >= 0) close(c->fd);
        free(c);
    }
}

/* Connect to a server. */
Connection *client_connect(const char *ip, uint16_t port) {
    if (!ip) { errno = EINVAL; return NULL; }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return NULL;

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &serv.sin_addr) != 1) {
        close(sockfd);
        errno = EINVAL;
        return NULL;
    }

    if (connect(sockfd, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
        close(sockfd);
        return NULL;
    }

    Connection *c = malloc(sizeof(Connection));
    if (!c) {
        close(sockfd);
        errno = ENOMEM;
        return NULL;
    }

    c->fd = sockfd;
    strncpy(c->peer_ip, ip, sizeof(c->peer_ip));
    c->peer_ip[sizeof(c->peer_ip)-1] = '\0';
    c->peer_port = port;
    c->color_id = 0; 
    c->stream_read = NULL;
    c->stream_write = NULL;
    return c;
}

/* Read up to len bytes from connection into buf. */
ssize_t conn_read(Connection *c, void *buf, size_t len) {
    if (!c || c->fd < 0) { errno = EBADF; return -1; }
    return read(c->fd, buf, len);
}

/* Write len bytes from buf to connection. */
ssize_t conn_write(Connection *c, const void *buf, size_t len) {
    if (!c || c->fd < 0) { errno = EBADF; return -1; }
    return write(c->fd, buf, len);
}