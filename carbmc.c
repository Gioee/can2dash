#include <xdo.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <malloc.h>
#include <semaphore.h>
#include <signal.h>
#include <arpa/inet.h>


#include "carbmc.h"

int sockfd = 0;

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/

int CarberryConnect()
{
  struct sockaddr_in serv_addr;

  while (true)
  {
    printf("Try to connecto to Carberry...\r\n");

    if (sockfd)
    {
      close(sockfd);
      sockfd = 0;
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
      printf("Error: Could not create socket\r\n");
      sleep(1);
      continue;
    }

    memset(&serv_addr, '0', sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(7070);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
    {
      printf("Error: inet_pton error occured\r\n");
      sleep(1);
      continue;
    }

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
      printf("Carberry Server Connection Failed. Sleep and retry\r\n");
      sleep(1);
      continue;
    }
    else
    {
      // Ok!
      printf("Carberry Ready\r\n");
      return true;
    }
  }
}

/*------------------------------------------------------------------------------
  Send a single line (CR/LF terminated) to socket
------------------------------------------------------------------------------*/

int SendSock(int *fd, char *buffer, unsigned int len)
{
  // Send to socket
  int sent = send(*fd, buffer, len, 0);
  if (sent <= 0)
  {
    // Maybe server has closed connection
    *fd = 0;
    return false;
  }
  else
  {
    return true;
  }
}

/*------------------------------------------------------------------------------
  Receive a single line (CR/LF terminated) from socket
  Timeout of 5 sec
------------------------------------------------------------------------------*/

int RecvSock(int *fd, char *buffer, unsigned int to)
{
  int i = 0;
  int j = 0;
  int nbytes = 0;
  struct timeval timeout;
  char tmp[STDSTR];
  fd_set reading;

  while (true)
  {
    FD_ZERO(&reading);
    FD_SET(*fd, &reading);
    // timeout
    timeout.tv_sec = to;
    timeout.tv_usec = 0;

    // Waiting for something
    switch (select(*fd + 1, &reading, NULL, NULL, &timeout))
    {
    // Error
    case -1:
      buffer[i] = 0;
      return false;
      break;
    // Timeout
    case 0:
      buffer[i] = 0;
      return false;
      break;
    // Data
    default:
      if (FD_ISSET(*fd, &reading))
      {
        // Get socket chars
        nbytes = recv(*fd, tmp, STDSTR, 0);
        // Error or closed
        if (nbytes <= 0)
        {
          *fd = 0;
          return false;
        }

        for (j = 0; j < nbytes; j++)
        {
          // Overflow
          if (i == (STDSTR - 1))
            return false;
          // Collect new char
          buffer[i++] = tmp[j];
          if (tmp[j] == '\n')
          {
            buffer[i++] = 0;
            return true;
          }
        }
      }
      break;
    }
  }
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/

int Talk(const char *format, ...)
{
  unsigned int lines = 1000;
  unsigned int tries = 3;
  char line[STDSTR];
  va_list ap;
  char ch;

  while (tries--)
  {
    // Prepare command
    va_start(ap, format);
    vsprintf(line, format, ap);
    va_end(ap);

    // Send command to server
    if (!SendSock(&sockfd, line, strlen(line)))
    {
      // Debug
      printf("Send Error!\r\n");
      CarberryConnect();
      continue;
    }
    while (lines--)
    {
      // Waiting for reply
      if (!RecvSock(&sockfd, line, 5))
      {
        // Debug
        printf("Recv Error!\r\n");
        if (!sockfd)
          CarberryConnect();
        break;
      }
      // Parse reply
      if (strstr(line, "OK"))
      {
        return true;
      }
      else if (strstr(line, "ERROR"))
      {
        return false;
      }
      else
      {
        // Get another line
      }
    }
  }
  return false;
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/

int ProcessEvents()
{
  char line[STDSTR];

  printf("Continue processing events...\r\n");

  xdo_t * x = xdo_new(NULL);

  while (true)
  {
    if (RecvSock(&sockfd, line, 1))
    {
      printf(line);
      printf("\r\nprova\r\n");

      printf(strncmp(line, "RX1 0206-01", 10));

      if (strncmp(line, "RX1 0206-01", 10))
      {
        char pulsante[2];
        memcpy(pulsante, &line[11], 2);
        int id = atoi(pulsante);
        switch (id)
        {
        case 81:
          printf("pulsante in alto a sinistra\r\n");
          break;
        case 82:
          printf("Pulsante giù a sinistra\r\n");
          break;
        case 83:
          printf("Manopola sinistra\r\n");
          break;
        case 84:
          printf("Pulsante manopola sinistra\r\n");
          break;
        case 91:
          printf("Pulsante destro in alto (successivo)\r\n");
          break;
        case 92:
          printf("Pulsante in basso a destra\r\n");
          xdo_send_keysequence_window(x, CURRENTWINDOW, "A", 0);
          break;
        case 93:
          printf("Manopola destra (Volume)\r\n");
          break;
        }
        id=0;
        strcpy(pulsante, "");
      }
    }
    else
    {
      // Try to reconnect
      if (!sockfd)
        CarberryConnect();
    }
  }
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
void SetupCarberry()
{
  printf("Setup Carberry interface...\r\n");

  // if (Talk("VERSION\r\n")) printf("Version Ok!\r\n");
  if (Talk("AT\r\n"))
    printf("AT Ok!\r\n");

  // Enable notification
  if (Talk("CAN USER FILTER CH1 0 0206\r\n"))
    printf("FILTRO INDEX 206 IMPOSTATO\r\n");
  if (Talk("CAN USER MASK CH1 0FFF\r\n"))
    printf("MASCHERA xFFF IMPOSTATA \r\n");
  // if (Talk("CAN USER ALIGN RIGHT\r\n"))  printf("CAN ALLINEATO A DESTRA\r\n");
  if (Talk("CAN USER OPEN CH1 95K2\r\n"))
    printf("CAN APERTO A 95K2\r\n");
  // if (Talk("SWC CONFIG SOURCE NOTIFY\r\n")) printf("Notify Ok!\r\n");
  // if (Talk("SWC CONFIG FACE NOTIFY\r\n"))   printf("Notify Ok!\r\n");
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
void terminal_kill_handler(int signum)
{
  printf("\r\nTerminal kill signal handled\r\n");
  close(sockfd);
  exit(0);
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
  printf("Carberry CAN2DASH Control\r\n");

  // Kill hook
  if (signal(SIGINT, terminal_kill_handler) == SIG_IGN)
    signal(SIGINT, SIG_IGN);
  if (signal(SIGTERM, terminal_kill_handler) == SIG_IGN)
    signal(SIGTERM, SIG_IGN);

  CarberryConnect();

  // SetupCarberry();
  ProcessEvents();

  return 0;
}
