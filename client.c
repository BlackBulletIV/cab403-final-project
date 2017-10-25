#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
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

#define BUFF_LEN 128

volatile int fd;

void exit_client(int code)
{
  fflush(stdout);
  shutdown(fd, SHUT_RDWR);
  close(fd);
  exit(code);
}

void login(char* username)
{
  int status;
  char password[MSG_LEN];

  fputs("==============================\n\n", stdout);
  fputs("Welcome to the Online Hangman Gaming System\n\n", stdout);
  fputs("==============================\n\n\n", stdout);
  fputs("You are required to logon with your registered username and password\n\n", stdout);
  fputs("Please enter your username: ", stdout);
  scanf("%s", username);
  fputs("Please enter your password: ", stdout);
  scanf("%s", password);

  if (sendstr(fd, username) == COMM_ERROR)
  {
    perror("send");
    exit_client(1);
  }

  if (sendstr(fd, password) == COMM_ERROR)
  {
    perror("send");
    exit_client(1);
  }

  fputs("\n\n", stdout);

  if (recvint(fd, &status) == COMM_ERROR)
  {
    perror("recv");
    exit_client(1);
  }
  else if (status == 0)
  {
    fputs("You entered either an incorrect username or password - disconnecting\n", stdout);
    fflush(stdout);
    exit_client(0);
  }
}

int main_menu()
{
  int scanned;
  int option = -1;
  char input[BUFF_LEN];

  fputs("Please enter an option\n", stdout);
  fputs("<1> Play Hangman\n", stdout);
  fputs("<2> Show Leaderboard\n", stdout);
  fputs("<3> Quit\n\n", stdout);

  do
  {
    fputs("Selection (1-3): ", stdout);
    fgets(input, BUFF_LEN, stdin);
    while (input[0] == '\n') fgets(input, BUFF_LEN, stdin);
    scanned = sscanf(input, "%i", &option);

    if (scanned != 1 || option < 1 || option > 3)
    {
      fputs("Option must be a number between 1 and 3.\n", stdout);
      option = -1;
    }
  } while(option == -1);
  
  fputs("\n", stdout);
  return option;
}

void play_game(char* name)
{
  int i, type_len, object_len, guesses_left, num_pos, status;
  char* words;
  char* guessed;
  int* positions;
  char input[BUFF_LEN];
  char guess;
  int guesses_taken = 0;
  char complete = 0;

  if (recvint(fd, &type_len) == COMM_ERROR)
  {
    perror("recvint");
    exit_client(1);
  }

  if (recvint(fd, &object_len) == COMM_ERROR)
  {
    perror("recvint");
    exit_client(1);
  }

  if (recvint(fd, &guesses_left) == COMM_ERROR)
  {
    perror("recvint");
    exit_client(1);
  }

  words = calloc(type_len + object_len, sizeof(char));
  guessed = malloc((guesses_left + 1) * sizeof(char));
  guessed[0] = '\0';

  fputs("================================\n\n", stdout);

  // will need socket error checking to prevent possible infinite loop
  while (1)
  {
    printf("Guessed letters: %s\n", guessed);
    printf("Number of guesses left: %i\n\n", guesses_left);
    fputs("Word: ", stdout);

    for (i = 0; i < type_len + object_len; ++i)
    {
      if (i == type_len) fputs(" ", stdout);

      if (words[i])
      {
        printf("%c ", words[i]);
      }
      else
      {
        fputs("_ ", stdout);
      }
    }

    fputs("\n\n", stdout);

    if (complete == 1)
    {
      if (status == 1)
      {
        printf("Well done %s! You won this round of Hangman!\n\n", name);
      }
      else if (status == 2)
      {
        printf("Bad luck %s! You have run out of guesses. The Hangman got you!\n\n", name);
      }

      fputs("--------------------------------\n\n", stdout);

      break;
    }

    guess = 0;

    do
    {
      fputs("Enter your guess: ", stdout);
      fgets(input, BUFF_LEN, stdin);
      while (input[0] == '\n') fgets(input, BUFF_LEN, stdin);

      if (input[0] >= 'a' && input[0] <= 'z')
      {
        guess = input[0];
      }
      else if (input[0] >= 'A' && input[0] <= 'Z')
      {
        guess = input[0] - 32; // convert to lower case
      }
      else
      {
        fputs("Guess must be a letter.\n", stdout);
      }
    } while(!guess);
    
    fputs("\n--------------------------------\n\n", stdout);

    if (sendint(fd, (int) guess) == COMM_ERROR)
    {
      perror("sendint");
      exit_client(1);
    }

    if (recvints(fd, &positions, &num_pos) == COMM_ERROR)
    {
      perror("sendints");
      exit_client(1);
    }

    if (recvint(fd, &status) == COMM_ERROR)
    {
      perror("recvint");
      exit_client(1);
    }

    guessed[guesses_taken] = guess;
    guessed[guesses_taken + 1] = '\0'; // to ensure a complete string
    guesses_taken++;
    guesses_left--;

    for (i = 0; i < num_pos; ++i)
    {
      words[positions[i]] = guess;
    }

    free(positions);
    if (status > 0) complete = 1;
  }
}

void show_leaderboard()
{
  char name[BUFF_LEN];
  int i, num_leaders, games_won, games_played;
  
  if (recvint(fd, &num_leaders) == COMM_ERROR)
  {
    perror("recvint");
    exit_client(1);
  }
  
  if (num_leaders > 0)
  {
    for (i = 0; i < num_leaders; ++i)
    {
      if (recvstr(fd, name) == COMM_ERROR)
      {
        perror("recvstr");
        exit_client(1);
      }

      if (recvint(fd, &games_won) == COMM_ERROR)
      {
        perror("recvint");
        exit_client(1);
      }

      if (recvint(fd, &games_played) == COMM_ERROR)
      {
        perror("recvint");
        exit_client(1);
      }

      fputs("================================\n\n", stdout);
      printf("Player :  %s\n", name);
      printf("Number of games won:    %i\n", games_won);
      printf("Number of games played: %i\n\n", games_played);
    }

    fputs("================================\n\n", stdout);
  }
  else
  {
    fputs("================================\n\n", stdout);
    fputs("There is no information currently in the leaderboard. Try again later.\n\n", stdout);
    fputs("================================\n\n", stdout);
  }
}

void sigint_handler(int signal)
{
  if (signal == SIGINT)
  {
    sendint(fd, TERMINATE_VALUE);
    printf("\nThank you for playing!\n");
    exit_client(0);
  }
}

int main(int argc, char* argv[])
{
  int port, rc;
  struct hostent* host;
  struct sockaddr_in server_addr;
  char username[MSG_LEN];

  if (argc < 3)
  {
    fputs("Client requires a hostname and port number to connect to\n", stdout);
    exit(1);
  }

  signal(SIGINT, sigint_handler);
  port = atoi(argv[2]);
  host = gethostbyname(argv[1]);

  if (host == NULL)
  {
    herror("gethostbyname");
    exit(1);
  }

  fd = socket(AF_INET, SOCK_STREAM, 0);

  if (fd == SOCKET_ERROR)
  {
    perror("socket");
    exit_client(1);
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr = *((struct in_addr*) host->h_addr);

  if (connect(fd, (struct sockaddr*) &server_addr, sizeof(struct sockaddr)) == -1)
  {
    perror("connect");
    exit_client(1);
  }

  login(username);

  while (1)
  {
    rc = main_menu();
    sendint(fd, rc);

    if (rc == 1)
    {
      play_game(username);
    }
    else if (rc == 2)
    {
      show_leaderboard();
    }
    else
    {
      fputs("Thank you for playing!\n", stdout);
      exit_client(0);
    }
  }

  return 0;
}