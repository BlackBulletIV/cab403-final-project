#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "socket_comms.h"

int sendstr(int fd, char* string)
{
  int i, rc;
  char str_ended = 0;
  uint16_t num;

  for (i = 0; i < MSG_LEN; ++i)
  {
    if (str_ended)
    {
      num = htons(0);
    }
    else
    {
      num = htons(string[i]);
      if (string[i] == '\0') str_ended = 1;
    }

    rc = send(fd, &num, sizeof(uint16_t), 0);
    if (rc == SOCKET_ERROR) return COMM_ERROR;
  }

  return COMM_NORMAL;
}

int recvstr(int fd, char* string)
{
  int i, rc;
  uint16_t num;

  for (i = 0; i < MSG_LEN; ++i)
  {
    rc = recv(fd, &num, sizeof(uint16_t), 0);
    if (rc == SOCKET_ERROR) return COMM_ERROR;
    if (ntohs(num) == TERMINATE_VALUE) return COMM_TERMINATE;
    string[i] = (char) ntohs(num);
  }

  return COMM_NORMAL;
}

int sendints(int fd, int* data, int size)
{
  int i, rc;
  uint16_t num = htons(size);
  rc = send(fd, &num, sizeof(uint16_t), 0);
  if (rc == SOCKET_ERROR) return COMM_ERROR;

  for (i = 0; i < size; ++i)
  {
    num = htons(data[i]);
    rc = send(fd, &num, sizeof(uint16_t), 0);
    if (rc == SOCKET_ERROR) return COMM_ERROR;
  }

  return COMM_NORMAL;
}

// uses malloc
int recvints(int fd, int** data, int* size)
{
  int* array;
  int i, rc;
  uint16_t num;

  rc = recv(fd, &num, sizeof(uint16_t), 0);
  if (rc == SOCKET_ERROR) return COMM_ERROR;
  if (ntohs(num) == TERMINATE_VALUE) return COMM_TERMINATE;
  *size = (int) ntohs(num);
  array = malloc(*size * sizeof(int));

  for (i = 0; i < *size; ++i)
  {
    rc = recv(fd, &num, sizeof(uint16_t), 0);

    if (rc == SOCKET_ERROR || ntohs(num) == TERMINATE_VALUE)
    {
      free(array);
      return rc == SOCKET_ERROR ? COMM_ERROR : COMM_TERMINATE;
    }

    array[i] = (int) ntohs(num);
  }

  *data = array;
  return COMM_NORMAL;
}

int sendint(int fd, int data)
{
  int rc;
  uint16_t num = htons(data);
  rc = send(fd, &num, sizeof(uint16_t), 0);
  if (rc == SOCKET_ERROR) return COMM_ERROR;
  return COMM_NORMAL;
}

int recvint(int fd, int* data)
{
  int rc;
  uint16_t num;
  rc = recv(fd, &num, sizeof(uint16_t), 0);
  if (rc == SOCKET_ERROR) return COMM_ERROR;
  if (ntohs(num) == TERMINATE_VALUE) return COMM_TERMINATE;
  *data = (int) ntohs(num);
  return COMM_NORMAL;
}
