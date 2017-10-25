#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "socket_comms.h"

// defs

#define DEFAULT_PORT 12345
#define NUM_THREADS 10
#define REQUEST_BACKLOG 10
#define MAX_GUESSES 26

#define GAME_WON 1
#define GAME_LOST 2

// structs

struct user
{
  char name[20];
  char password[20];
};

typedef struct user user_t;

struct leader
{
  int user_id;
  int games_won;
  int games_played;
  struct leader* prev;
  struct leader* next;
};

typedef struct leader leader_t;

struct word
{
  char type[30];
  char object[30];
  int type_len;
  int object_len;
};

typedef struct word word_t;

struct request
{
  int fd;
  struct request* next;
};

typedef struct request request_t;

// global variables

word_t* words;
int num_words = 0;

user_t* users;
int num_users = -1; // subtract the initial header in Authentication.txt

leader_t* leader_head = NULL;
leader_t* leader_tail = NULL;
int num_leaders = 0;

request_t* wait_head = NULL;
request_t* wait_tail = NULL;

volatile char run_server = 1;

// synchronisation

pthread_mutex_t wait_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
pthread_cond_t got_request = PTHREAD_COND_INITIALIZER;

sem_t write_mutex; // needs to be semaphore so other threads can lock/unlock
pthread_mutex_t read_count_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
int read_count = 0;

// data parsing functions

void parse_users()
{
  FILE* fp;
  char* line = NULL;
  size_t len = 0;
  ssize_t read;
  int i, j;
  char after_white;

  fp = fopen("Authentication.txt", "r");

  if (fp == NULL)
  {
    perror("fopen");
    exit(1);
  }

  while ((read = getline(&line, &len, fp)) != -1)
  {
    num_users++;
  }

  users = malloc(num_users * sizeof(*users));
  rewind(fp);

  for (i = -1; (read = getline(&line, &len, fp)) != -1; ++i)
  {
    if (i == -1) continue; // skip first header line
    after_white = 0;
    j = 0;

    while (1)
    {
      if (line[j] == '\n' || line[j] == '\r' || line[j] == '\0')
      {
        users[i].password[j - after_white] = '\0';
        break; 
      }
      else if (line[j] != ' ' && line[j] != '\t')
      {
        // first character of password
        if ((j > 0 && line[j - 1] == ' ') || line[j - 1] == '\t')
        {
          after_white = j;
        }

        if (after_white)
        {
          users[i].password[j - after_white] = line[j];
        }
        else
        {
          users[i].name[j] = line[j];
        }

        // last character of name
        if (line[j + 1] == ' ' || line[j + 1] == '\t')
        {
          users[i].name[j + 1] = '\0';
        }
      }

      j++;
    }
  }

  fclose(fp);
}

void parse_words()
{
  FILE* fp;
  char* line = NULL;
  size_t len = 0;
  ssize_t read;
  int i, j;
  char after_comma;

  fp = fopen("hangman_text.txt", "r");

  if (fp == NULL)
  {
    perror("fopen");
    exit(1);
  }

  while ((read = getline(&line, &len, fp)) != -1)
  {
    num_words++;
  }

  words = malloc(num_words * sizeof(*words));
  rewind(fp);

  for (i = 0; (read = getline(&line, &len, fp)) != -1; ++i)
  {
    after_comma = 0;
    j = 0;

    while (1)
    {
      if (line[j] == '\n' || line[j] == '\r' || line[j] == '\0')
      {
        words[i].type[j - after_comma] = '\0';
        break;
      }
      else if (line[j] == ',')
      {
        after_comma = j + 1;
        words[i].object[j] = '\0';
      }
      else if (after_comma)
      {
        words[i].type[j - after_comma] = line[j];
      }
      else
      {
        words[i].object[j] = line[j];
      }

      j++;
    }

    words[i].type_len = strlen(words[i].type);
    words[i].object_len = strlen(words[i].object);
  }

  fclose(fp);
}

// client handling functions
void update_leaderboard(int user_id, int status)
{
  leader_t* entry;
  leader_t* l;
  char create_new = 1;
  char insert = 0;

  if (leader_head != NULL)
  {
    for (entry = leader_head; entry != NULL; entry = entry->next)
    {
      if (entry->user_id == user_id)
      {
        create_new = 0;
        break;
      }
    }
  }

  if (create_new)
  {
    entry = malloc(sizeof(*entry));
    entry->user_id = user_id;
    entry->games_won = status == GAME_WON ? 1 : 0;
    entry->games_played = 1;
    entry->prev = NULL;
    entry->next = NULL;
    insert = 1;
    num_leaders++;
  }
  else
  {
    entry->games_played++;

    if (status == GAME_WON)
    {
      entry->games_won++;
      insert = 1;

      // remove entry from list for reinsertion
      if (entry->next != NULL)
      {
        if (entry->prev == NULL)
        {
          entry->next->prev = NULL;
          leader_head = entry->next;
        }
        else
        {
          entry->next->prev = entry->prev;
        }
      }

      if (entry->prev != NULL)
      {
        if (entry->next == NULL)
        {
          entry->prev->next = NULL;
          leader_tail = entry->prev;
        }
        else
        {
          entry->prev->next = entry->next;
        }
      }

      if (leader_head == entry && leader_tail == entry)
      {
        leader_head = NULL;
        leader_tail = NULL;
      }
    }
  }

  if (insert)
  {
    if (leader_head == NULL)
    {
      leader_head = entry;
      leader_tail = entry;
    }
    else
    {
      // ascending order based on games_won
      for (l = leader_head; l != NULL; l = l->next)
      {
        if (l->games_won < entry->games_won)
        {
          if (l->next == NULL) // insertion at tail
          {
            entry->prev = l;
            l->next = entry;
            leader_tail = entry;
            break;
          }
          else if (l->next->games_won > entry->games_won) // insertion after l
          {
            entry->prev = l;
            entry->next = l->next;
            l->next->prev = entry;
            l->next = entry;
            break;
          }
        }
        else if (l->prev == NULL) // insertion at head
        {
          entry->next = l;
          l->prev = entry;
          leader_head = entry;
          break;
        }
        else if (l->prev->games_won < entry->games_won) // insertion before l
        {
          entry->prev = l->prev;
          entry->next = l;
          l->prev->next = entry;
          l->prev = entry;
          break;
        }
      }
    }
  }
}

int send_leaderboard(int fd)
{
  leader_t* l;
  if (sendint(fd, num_leaders) == COMM_ERROR) return COMM_ERROR;

  for (l = leader_head; l != NULL; l = l->next)
  {
    if (sendstr(fd, users[l->user_id].name) == COMM_ERROR) return COMM_ERROR;
    if (sendint(fd, l->games_won) == COMM_ERROR) return COMM_ERROR;
    if (sendint(fd, l->games_played) == COMM_ERROR) return COMM_ERROR;
  }

  return 0;
}

int play_game(int fd, int* status)
{
  char guessed[MAX_GUESSES + 1];
  word_t* word;
  int i, combined_len, chars_left, guesses_left, guess;
  int* positions;
  char char_found = 0;
  char match_found = 0;
  int guesses_taken = 0;
  int matches = 0;

  *status = 0;
  guessed[0] = '\0';

  word = &words[(int) floor(((double) rand()) / RAND_MAX * num_words + 0.5)];
  combined_len = word->type_len + word->object_len;
  chars_left = combined_len;
  positions = calloc(combined_len, sizeof(int));

  guesses_left = combined_len + 10;
  if (guesses_left > MAX_GUESSES) guesses_left = MAX_GUESSES;
  
  if (sendint(fd, word->type_len) == COMM_ERROR) return COMM_ERROR;
  if (sendint(fd, word->object_len) == COMM_ERROR) return COMM_ERROR;
  if (sendint(fd, guesses_left) == COMM_ERROR) return COMM_ERROR;

  while (!(*status))
  {
    if (recvint(fd, &guess) == COMM_ERROR) return COMM_ERROR;
    char_found = 0;

    for (i = 0; guessed[i] != '\0'; ++i)
    {
      if (guess == guessed[i])
      {
        char_found = 1;
        break;
      }
    }

    matches = 0;
    memset(positions, 0, sizeof(int) * combined_len);

    if (!char_found)
    {
      for (i = 0; i < combined_len; ++i)
      {
        match_found = 0;

        if (i < word->type_len)
        {
          if (word->type[i] == guess) match_found = 1;
        }
        else if (word->object[i - word->type_len] == guess)
        {
          match_found = 1;
        }

        if (match_found)
        {
          positions[matches] = i;
          matches++;
          chars_left--;
        }
      }
    }

    guessed[guesses_taken] = guess;
    guessed[guesses_taken + 1] = '\0'; // to ensure a complete string
    guesses_taken++;
    guesses_left--;

    if (chars_left <= 0)
    {
      *status = GAME_WON;
    }
    else if (guesses_left <= 0)
    {
      *status = GAME_LOST;
    }

    if (sendints(fd, positions, matches) == COMM_ERROR) return COMM_ERROR;
    if (sendint(fd, *status) == COMM_ERROR) return COMM_ERROR;
  }

  free(positions);
  return 0;
}

void handle_client(int thread_id, int fd)
{
  char username[MSG_LEN];
  char password[MSG_LEN];
  int user_id = -1;
  int i, rc, code, status;

  if (recvstr(fd, username) == COMM_ERROR)
  {
    printf("recv error (username): closing client #%i\n", fd);
    fflush(stdout);
    return;
  }

  if (recvstr(fd, password) == COMM_ERROR)
  {
    printf("recv error (password): closing client #%i\n", fd);
    fflush(stdout);
    return;
  }

  for (i = 0; i < num_users; ++i)
  {
    if (strcmp(username, users[i].name) == 0)
    {
      user_id = i;
      break;
    }
  }

  if (user_id == -1 || strcmp(password, users[user_id].password) != 0)
  {
    sendint(fd, 0); // login rejection
    return;
  }

  // login success
  if (sendint(fd, 1) == COMM_ERROR)
  {
    printf("send error (login success): closing client #%i\n", fd);
    fflush(stdout);
    return;
  }

  while (1)
  {
    // action code
    if (recvint(fd, &code) == COMM_ERROR)
    {
      printf("recv error (action code): closing client #%i\n", fd);
      fflush(stdout);
      return;
    }

    if (code == 1)
    {
      rc = play_game(fd, &status);

      if (rc == COMM_ERROR)
      {
        printf("comm error (play_game): closing client #%i\n", fd);
        fflush(stdout);
        return;
      }

      sem_wait(&write_mutex);
      update_leaderboard(user_id, status);
      sem_post(&write_mutex);
    }
    else if (code == 2)
    {
      pthread_mutex_lock(&read_count_mutex);
      read_count++;
      if (read_count == 1) sem_wait(&write_mutex);
      pthread_mutex_unlock(&read_count_mutex);

      rc = send_leaderboard(fd);

      pthread_mutex_lock(&read_count_mutex);
      read_count--;
      if (read_count == 0) sem_post(&write_mutex);
      pthread_mutex_unlock(&read_count_mutex);

      if (rc == COMM_ERROR)
      {
        printf("comm error (send_leaderboard): closing client #%i\n", fd);
        fflush(stdout);
        return;
      }
    }
    else
    {
      return;
    }
  }
}

void* thread_cleanup(void* data)
{
  request_t* req = *((request_t**) data);

  if (req != NULL)
  {
    sendint(req->fd, TERMINATE_VALUE);
    shutdown(req->fd, SHUT_RDWR);
    close(req->fd);
    free(req);
  }

  pthread_mutex_unlock(&wait_mutex);
}

void* handle_request(void* data)
{
  int thread_id = *((int*) data);
  request_t* req = NULL;

  pthread_cleanup_push(thread_cleanup, (void*) &req);
  pthread_mutex_lock(&wait_mutex);

  while (1)
  {
    pthread_testcancel();

    if (wait_head != NULL)
    {
      req = wait_head;

      if (wait_head->next != NULL)
      {
        wait_head = wait_head->next;
      }
      else
      {
        wait_head = NULL;
        wait_tail = NULL;
      }

      pthread_mutex_unlock(&wait_mutex);

      printf("client connected (#%i)\n", req->fd);
      fflush(stdout);

      handle_client(thread_id, req->fd);
      shutdown(req->fd, SHUT_RDWR);
      close(req->fd);

      printf("client disconnected (#%i)\n", req->fd);
      fflush(stdout);
      free(req);
      req = NULL;

      pthread_mutex_lock(&wait_mutex);
    }
    else
    {
      pthread_cond_wait(&got_request, &wait_mutex);
    }
  }

  pthread_cleanup_pop(thread_cleanup);
}

void cleanup_globals()
{
  request_t* request;
  request_t* tmp_request;
  leader_t* leader;
  leader_t* tmp_leader;

  request = wait_head;

  // clear any pending requests
  while (request != NULL)
  {
    sendint(request->fd, TERMINATE_VALUE);
    shutdown(request->fd, SHUT_RDWR);
    close(request->fd);
    tmp_request = request->next;
    free(request);
    request = tmp_request;
  }

  leader = leader_head;

  while (leader != NULL)
  {
    tmp_leader = leader->next;
    free(leader);
    leader = tmp_leader;
  }

  free(users);
  free(words);
}

// signal handler

void sigint_handler(int signal)
{
  if (signal == SIGINT) run_server = 0;
}

// main function

int main(int argc, char* argv[])
{
  int socket_fd, client_fd, rc, i;
  int thread_ids[NUM_THREADS];
  pthread_t threads[NUM_THREADS];

  struct sockaddr_in server_addr;
  struct sockaddr_in client_addr;
  socklen_t sin_size = sizeof(struct sockaddr *);
  int server_port = DEFAULT_PORT;

  request_t* request;
  struct timespec sleep_spec;

  srand((unsigned) time(NULL));
  signal(SIGINT, sigint_handler);
  sleep_spec.tv_sec = 0;
  sleep_spec.tv_nsec = 10000;
  rc = sem_init(&write_mutex, 0, 1);

  if (rc != 0)
  {
    perror("sem_init");
    exit(1);
  }

  parse_users();
  parse_words();

  if (argc > 1)
  {
    server_port = atoi(argv[1]);
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(server_port);
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  // create socket
  socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

  if (socket_fd == SOCKET_ERROR)
  {
    perror("socket");
    exit(1);
  }

  // bind socket
  if (bind(socket_fd, (struct sockaddr*) &server_addr, sizeof(struct sockaddr)) == -1)
  {
    perror("bind");
    close(socket_fd);
    exit(1);
  }

  // listen on socket
  if (listen(socket_fd, REQUEST_BACKLOG) == SOCKET_ERROR)
  {
    perror("listen");
    close(socket_fd);
    exit(1);
  }

  // create threadpool
  for (i = 0; i < NUM_THREADS; ++i)
  {
    thread_ids[i] = i;
    pthread_create(&threads[i], NULL, handle_request, (void*) &thread_ids[i]);
  }

  // handle incoming client requests
  while (run_server)
  {
    client_fd = accept(socket_fd, (struct sockaddr*) &client_addr, &sin_size);

    if (client_fd == SOCKET_ERROR)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        nanosleep(&sleep_spec, NULL);
      }
      else
      {
        perror("accept");
      }

      continue;
    }

    request = malloc(sizeof(*request));
    request->fd = client_fd;
    request->next = NULL;

    pthread_mutex_lock(&wait_mutex);

    if (wait_head == NULL)
    {
      wait_head = request;
      wait_tail = request;
    }
    else
    {
      wait_tail->next = request;
      wait_tail = request;
    }

    pthread_cond_signal(&got_request);
    pthread_mutex_unlock(&wait_mutex);
  }

  // cancel threads
  for (i = 0; i < NUM_THREADS; ++i)
  {
    pthread_cancel(threads[i]);
    pthread_join(threads[i], NULL);
  }

  cleanup_globals();
  close(socket_fd);
  return 0;
}
