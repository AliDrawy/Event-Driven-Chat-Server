#include <stdio.h>

#include <string.h>

#include <unistd.h>

#include <stdlib.h>

#include <sys/socket.h>

#include <arpa/inet.h>

#include <signal.h>

#include <sys/ioctl.h>

#include <sys/select.h>

#include "chatServer.h"

#define FALSE 0

static int end_server = 0;
int fd_srv;
//this function when the welcome socket write a ^c to stop the code so i can make free to the data
void intHandler(int SIG_INT) {
    end_server = 1;
    if (SIG_INT == SIGINT) {
        return;
    }
}

int main(int argc, char * argv[]) {
    if (argc != 2) {
        printf("Usage: chatServer <port>\n");
        exit(1);
    }
    signal(SIGINT, intHandler);
    struct sockaddr_in server;
    conn_pool_t * pool = malloc(sizeof(conn_pool_t));
    if (pool == NULL) {
       printf("Allocation failure: \n");
        exit(1);
    }
    init_pool(pool);
    int port = atoi(argv[1]);
    fd_srv = socket(PF_INET, SOCK_STREAM, 0);
    if (fd_srv < 0) {
        perror("error: socket\n");
        exit(1);
    }
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    int on = 1, io;
    io = ioctl(fd_srv, (int) FIONBIO, (char * ) & on);
    if (io < 0) {
        perror("error: ioctl\n");
        exit(1);
    }
    if (bind(fd_srv, (struct sockaddr * ) & server, sizeof(server)) < 0) {
        perror("error: bind\n");
        exit(1);
    }
    if (listen(fd_srv, 10) < 0) {
        perror("error: listen\n");
        exit(1);
    }
    int * newfd = (int * ) malloc(sizeof(int)), r;
    struct sockaddr_in client;
    int client_len = sizeof(client);
    FD_ZERO( & pool -> read_set);
    FD_SET(fd_srv, & pool -> read_set);
    pool -> maxfd = fd_srv;
    do {
        FD_ZERO( & pool -> ready_read_set);
        FD_ZERO( & pool -> ready_write_set);
        pool -> ready_read_set = pool -> read_set;
        pool -> ready_write_set = pool -> write_set;
        printf("Waiting on select()...\nMaxFd %d\n", pool -> maxfd);
        pool -> nready = select(pool -> maxfd + 1, & pool -> ready_read_set, & pool -> ready_write_set, NULL, NULL);
        if (pool -> nready < 0) {
            break;
        }
        int maxtmp = pool -> maxfd;
        for (int i = 3; i <= maxtmp; ++i) {
            if (FD_ISSET(i, & pool -> ready_read_set)) {
                if (i == fd_srv) {
                    newfd[0] = accept(fd_srv, (struct sockaddr * ) & client, (socklen_t * ) & client_len);
                    if (newfd[0] < 0) {
                        FD_CLR(newfd[0], & pool -> ready_read_set);
                        continue;
                    }
                    if (add_conn(newfd[0], pool) == -1) {
                        continue;
                    }
                    FD_CLR(newfd[0], & pool -> ready_read_set);
                } else {
                    printf("Descriptor %d is readable\n", i);
                    char buffer[4096];
                    memset(buffer, '\0', 4096);
                    r = (int) read(i, buffer, 4096);
                    if (r < 0) {
                        continue;
                    }

                    if (r == 0) {
                        remove_conn(i, pool);
                        printf("Connection closed for sd %d\n", i);
                        printf("removing connection with sd %d \n", i);
                        continue;
                    } else {
                        printf("%d bytes received from sd %d\n", r, i);
                        add_msg(i, buffer, r, pool);

                    }
                }
            }
            if (FD_ISSET(i, & pool -> ready_write_set)) {
                /* try to write all msgs in queue to sd */

                if (write_to_client(i, pool) == -1) {
                    continue;
                }
            }
        }
        /* End of loop through selectable descriptors */

    } while (end_server == FALSE);
    free(newfd);
    conn_t * temp = pool -> conn_head, * temp2;
    while (temp != NULL) {
        temp2 = temp -> next;
        printf("removing connection with sd %d \n", temp -> fd);
        remove_conn(temp -> fd, pool);
        temp = temp2;
    }
    FD_CLR(fd_srv, & pool -> ready_read_set);
    FD_CLR(fd_srv, & pool -> read_set);
    FD_CLR(fd_srv, & pool -> ready_write_set);
    FD_CLR(fd_srv, & pool -> write_set);
    close(fd_srv);
    free(pool);
    return 0;
}
//make initalize to the pool
int init_pool(conn_pool_t * pool) {
    pool -> maxfd = 0;
    pool -> nr_conns = 0;
    pool -> conn_head = NULL;
    pool -> nready = 0;
    FD_ZERO( & pool -> read_set);
    FD_ZERO( & pool -> ready_read_set);
    FD_ZERO( & pool -> ready_read_set);
    FD_ZERO( & pool -> write_set);
    return 0;
}
//add a new connection(new client) to the link list in the pool
int add_conn(int sd, conn_pool_t * pool) {
    conn_t * new_conn = (conn_t * ) malloc(sizeof(conn_t));
    if (!new_conn) {
        printf("Allocation failure: \n");
        return -1;
    }
    printf("New incoming connection on sd %d\n", sd);
    new_conn -> fd = sd;
    new_conn -> next = NULL;
    new_conn -> prev = NULL;
    new_conn -> write_msg_head = NULL;
    new_conn -> write_msg_tail = NULL;

    FD_SET(sd, & pool -> read_set);

    if (pool -> maxfd < new_conn -> fd) {
        pool -> maxfd = new_conn -> fd;
    }
    if (pool -> conn_head == NULL) {
        pool -> conn_head = new_conn;

    } else {
        conn_t * temp_conn;
        temp_conn = pool -> conn_head;
        while (temp_conn -> next != NULL) {
            temp_conn = temp_conn -> next;
        }
        temp_conn -> next = new_conn;
        new_conn -> prev = temp_conn;

    }
    pool -> nr_conns++;
    return 0;
}
//remove a  connection( client) from the link list in the pool
int remove_conn(int sd, conn_pool_t * pool) {
    FD_CLR(sd, & pool -> read_set);
    FD_CLR(sd, & pool -> ready_read_set);
    FD_CLR(sd, & pool -> write_set);
    FD_CLR(sd, & pool -> ready_write_set);
    conn_t * temp;

    if (pool -> conn_head -> next == NULL && pool -> conn_head -> fd == sd) {
        pool -> maxfd = fd_srv;
        pool -> nr_conns = 0;
        msg_t * temp_msg = pool -> conn_head -> write_msg_head;
        msg_t * nxt_msg;
        while (temp_msg != NULL) {
            nxt_msg = temp_msg -> next;
            free(temp_msg -> message);
            temp_msg -> size = 0;
            free(temp_msg);
            temp_msg = nxt_msg;
        }
        free(pool -> conn_head);
        return 0;
    }
    if (pool -> maxfd == sd) {
        conn_t * tmp;
        tmp = pool -> conn_head;
        int sec_max_fd = 0;
        while (tmp != NULL) {
            if (sec_max_fd < tmp -> fd && tmp -> fd != sd) {
                sec_max_fd = tmp -> fd;
            }
            tmp = tmp -> next;
        }
        pool -> maxfd = sec_max_fd;
    }
    temp = pool -> conn_head;
    while (temp != NULL) {
        if (temp -> fd == sd) {
            if (temp -> fd == pool -> conn_head -> fd) {
                pool -> conn_head = pool -> conn_head -> next;
                pool -> conn_head -> prev = NULL;
            }
            pool -> nr_conns--;
            if (temp -> prev != NULL) {
                temp -> prev -> next = temp -> next;
            }
            msg_t * temp_msg = temp -> write_msg_head;
            msg_t * nxt_msg;
            while (temp_msg != NULL) {
                nxt_msg = temp_msg -> next;
                free(temp_msg -> message);
                temp_msg -> size = 0;
                free(temp_msg);
                temp_msg = nxt_msg;
            }
            free(temp);
            close(sd);
            return 0;
        }
        temp = temp -> next;
    }

    return 1;
}
//Add msg to the queues of all connections except of the origin
int add_msg(int sd, char * buffer, int len, conn_pool_t * pool) {

    conn_t * temp = pool -> conn_head;
    while (temp != NULL) {
        if (temp -> fd != sd) {
            msg_t * new_msg = (msg_t * ) malloc(sizeof(msg_t));
            new_msg -> next = NULL;
            new_msg -> size = len;
            new_msg -> message = (char * ) malloc(len + 1);
            strcpy(new_msg -> message, buffer);
            new_msg -> message[len] = '\0';
            if (temp -> write_msg_head == NULL) {
                temp -> write_msg_head = new_msg;
                temp -> write_msg_tail = new_msg;

            } else {
                temp -> write_msg_tail -> next = new_msg;
                temp -> write_msg_tail = temp -> write_msg_tail -> next;

            }

            FD_SET(temp -> fd, & pool -> write_set);
        }
        temp = temp -> next;
    }
    return 0;
}
//Write all the messages  to client
int write_to_client(int sd, conn_pool_t * pool) {

    conn_t * temp = pool -> conn_head;
    while (temp != NULL) {
        if (temp -> fd == sd) {
            msg_t * temp_msg = temp -> write_msg_head;

            while (temp_msg != NULL) {
                int w = (int) write(temp -> fd, temp_msg -> message, temp_msg -> size);
                if (w <= 0) {
                    return -1;
                }
                temp -> write_msg_head = temp -> write_msg_head -> next;
                free(temp_msg -> message);
                free(temp_msg);
                temp_msg = temp -> write_msg_head;

            }
            FD_CLR(temp -> fd, & pool -> write_set);
            FD_CLR(temp -> fd, & pool -> ready_write_set);
            break;
        }
        temp = temp -> next;
    }
    return 0;
}
