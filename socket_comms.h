#ifndef SOCKET_COMMS_H
#define SOCKET_COMMS_H

#include <limits.h>

#define SOCKET_ERROR -1
#define COMM_ERROR 1
#define COMM_TERMINATE COMM_ERROR // ensures only a check for COMM_ERROR must be performed in code
#define COMM_NORMAL 0
#define TERMINATE_VALUE USHRT_MAX
#define MSG_LEN 128

int sendstr(int fd, char* string);
int recvstr(int fd, char* string);
int sendints(int fd, int* data, int size);
int recvints(int fd, int** data, int* size);
int sendint(int fd, int data);
int recvint(int fd, int* data);

#endif
