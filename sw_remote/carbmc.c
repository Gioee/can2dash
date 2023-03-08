
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

#include "carbmc.h"

int sockfd = 0;
int xbmcfd = 0;
int divert = true;

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
      //Ok!
      printf("Carberry Ready\r\n");
      return true;
    }
  }
}
 
/*------------------------------------------------------------------------------
  Send a single line (CR/LF terminated) to socket
------------------------------------------------------------------------------*/
int SendSock(int* fd, char* buffer, unsigned int len)
{
  //Send to socket
  int sent = send(*fd, buffer, len, 0);
  if (sent <= 0)
  {
    //Maybe server has closed connection
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
int RecvSock(int* fd, char* buffer, unsigned int maxlen, unsigned int to)
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
    //timeout
    timeout.tv_sec  = to;
    timeout.tv_usec = 0;

    //Waiting for something
    switch (select(*fd+1, &reading, NULL, NULL, &timeout))
    {
      //Error
      case -1:
        buffer[i] = 0;
        return false;
      break;
      //Timeout
      case 0:
        buffer[i] = 0;
        return false;
      break;
      //Data
      default:
        if (FD_ISSET(*fd, &reading))
        {
          //Get socket chars
          nbytes = recv(*fd, tmp, STDSTR, 0);
          //Error or closed
          if (nbytes <= 0)
          {
            *fd = 0;
            return false;
          }

          for (j=0; j<nbytes; j++)
          {
            //Overflow
            if (i == (maxlen-1)) return false;
            //Collect new char
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
    //Prepare command
    va_start(ap, format);
    vsprintf(line, format, ap);
    va_end(ap);

    //Send command to server
    if (!SendSock(&sockfd, line, strlen(line)))
    {
      //Debug
      printf("Send Error!\r\n");
      CarberryConnect();
      continue;
    }
    while (lines--)
    {
      //Waiting for reply
      if (!RecvSock(&sockfd, line, STDSTR, 5))
      {
        //Debug
        printf("Recv Error!\r\n");
        if (!sockfd) CarberryConnect();
        break;
      }
      //Parse reply
      if      (strstr(line, "OK"))
      {
        return true;
      }
      else if (strstr(line, "ERROR"))
      {
        return false;
      }
      else
      {
        //Get another line
      }
    }
  }
  return false;
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int SendXBMC(char* command)
{
  while (!SendSock(&xbmcfd, command, strlen(command)))
  {
    XBMCServConnect();
  }
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int TermExtractUntil(char** p, char* word, char token, unsigned int len)
{
  char ch;
  unsigned int j = 0;

  while (true)
  {
    ch = **p;
    (*p)++;

    //End of string
    if (!ch)
    {
      (*p)--;//Don't cross null!
      word[j] = 0;
      if (j) return true; else return false;
    }
    if ((ch == token) || (ch == '\r') || (ch == '\n'))
    {
      word[j] = 0;
      return true;
    }
    //Add new char
    word[j] = ch;
    //Check space
    if (j<(len-1))
    {
      j++;
    }
    else
    {
      word[j] = 0;
      return false;
    }
  }
  return true;
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int TermEvntDynamic(tTermEvntTbl* dyncmds, int pos, int idx, char** p)
{
  int i;
  char word[17];

  //Extract a piece of string
  TermExtractUntil(p, word, ' ', sizeof(word));
  //Test
  if (!strlen(word))
  {
    printf("Broken Command\r\n");
    return false;
  }

  //Iterate through commands
  for (i=0; i < dyncmds->nrows; i++)
  {
    //printf("Evaluating... %s\r\n", dyncmds->rows[i].text);
    //Match?
    if (!strcasecmp(word, dyncmds->rows[i].text))
    {
      //printf("Match!\r\n");
      //Execute function
      if ((*dyncmds->rows[i].func)((dyncmds->rows[i].param), i, idx ,p))
      {
        //Recursively exit
        return true;
      }
      else
      {
        return false;
      }
    }
  }
  //printf("Unknown Command %s\r\n", word);
  return false;
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int TermCmdNimp(void* param, int pos, int idx, char** p)
{
  printf("Not yet implemented!\r\n");
  return true;
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int TermKeyKindClickVolum(void* param, int pos, int idx, char** p)
{
  printf("Click Vol-\r\n");
  return true;
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int TermKeyKindClickVolup(void* param, int pos, int idx, char** p)
{
  printf("Click Vol+\r\n");
  return true;
}

/*------------------------------------------------------------------------------
 -----------------------------------------------------------------------------*/
int TermKeyKindClickSeekm(void* param, int pos, int idx, char** p)
{
  printf("Click Seek-\r\n");
  if (divert)
  {
    SendXBMC("{\"jsonrpc\": \"2.0\", \"method\": \"Input.Left\"}");
  }
  return true;
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int TermKeyKindClickSeekp(void* param, int pos, int idx, char** p)
{
  printf("Click Seek+\r\n");
  if (divert)
  {
    SendXBMC("{\"jsonrpc\": \"2.0\", \"method\": \"Input.Right\"}");
  }
  return true;
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int TermKeyKindClickSource(void* param, int pos, int idx, char** p)
{
  printf("Click Source\r\n");
  if (divert)
  {
    SendXBMC("{\"jsonrpc\": \"2.0\", \"method\": \"Input.Select\"}");
    SendXBMC("{\"jsonrpc\": \"2.0\", \"method\": \"Input.ShowOSD\"}");
  }
  return true;
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int TermKeyKindClickMute(void* param, int pos, int idx, char** p)
{
  printf("Click Mute\r\n");
  return true;
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int TermKeyKindClickFace(void* param, int pos, int idx, char** p)
{
  printf("Click Face\r\n");
  SendXBMC("{\"jsonrpc\":\"2.0\",\"method\":\"Input.ExecuteAction\",\"params\": { \"action\": \"fullscreen\" }, \"id\": 1}");
  return true;
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int TermKeyKindClickAnswer(void* param, int pos, int idx, char** p)
{
  printf("Click Answer\r\n");
  return true;
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int TermKeyKindClickHangup(void* param, int pos, int idx, char** p)
{
  printf("Click Hangup\r\n");
  return true;
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int TermKeyKindHoldVolum(void* param, int pos, int idx, char** p)
{
  printf("Hold Vol-\r\n");
  return true;
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int TermKeyKindHoldVolup(void* param, int pos, int idx, char** p)
{
  printf("Hold Vol+\r\n");
  return true;
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int TermKeyKindHoldSeekm(void* param, int pos, int idx, char** p)
{
  printf("Hold Seek-\r\n");
  if (divert)
  {
    SendXBMC("{\"jsonrpc\": \"2.0\", \"method\": \"Input.Up\"}");
  }
  return true;
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int TermKeyKindHoldSeekp(void* param, int pos, int idx, char** p)
{
  printf("Hold Seek+\r\n");
  if (divert)
  {
    SendXBMC("{\"jsonrpc\": \"2.0\", \"method\": \"Input.Down\"}");
  }
  return true;
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int TermKeyKindHoldSource(void* param, int pos, int idx, char** p)
{
  printf("Hold Source\r\n");
  if (divert)
  {
    SendXBMC("{\"jsonrpc\": \"2.0\", \"method\": \"Input.Back\"}");
  }
  return true;
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int TermKeyKindHoldMute(void* param, int pos, int idx, char** p)
{
  printf("Hold Mute\r\n");
  return true;
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int TermKeyKindHoldFace(void* param, int pos, int idx, char** p)
{
  printf("Hold Face\r\n");
  SendXBMC("{\"jsonrpc\":\"2.0\",\"method\":\"Input.ExecuteAction\",\"params\": { \"action\": \"fullscreen\" }, \"id\": 1}");
  return true;
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int TermKeyKindHoldAnswer(void* param, int pos, int idx, char** p)
{
  printf("Hold Answer\r\n");
  return true;
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int TermKeyKindHoldHangup(void* param, int pos, int idx, char** p)
{
  printf("Hold Hangup\r\n");
  return true;
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int TermKeySequence(void* param, int pos, int idx, char** p)
{
  char word[17];

  //Extract a piece of string
  TermExtractUntil(p, word, 0, sizeof(word));

  int seq = atoi(word);
  
  /*switch (seq)
  {
    case 1:
      printf("Detected #1 sequence\r\n");
      if (!divert)
      {
        //Divert this commands when interface is on
        if (Talk("SWC CONFIG SEEKM BYPASS\r\n"))  printf("Bypass Ok!\r\n");
        if (Talk("SWC CONFIG SEEKP BYPASS\r\n"))  printf("Bypass Ok!\r\n");
        if (Talk("SWC CONFIG SOURCE BYPASS\r\n")) printf("Bypass Ok!\r\n");
        if (Talk("SWC CONFIG FACE BYPASS\r\n"))   printf("Bypass Ok!\r\n");
        SendXBMC("{\"jsonrpc\": \"2.0\", \"method\": \"GUI.ShowNotification\", \"params\":{\"title\":\"Carberry\",\"message\":\"Steeringwheel Commands ON!\"}}");
        divert = true;
      }
      else
      {
        //Divert this commands when interface is on
        if (Talk("SWC CONFIG SEEKM PASSTHRU\r\n"))  printf("Passthru Ok!\r\n");
        if (Talk("SWC CONFIG SEEKP PASSTHRU\r\n"))  printf("Passthru Ok!\r\n");
        if (Talk("SWC CONFIG SOURCE PASSTHRU\r\n")) printf("Passthru Ok!\r\n");
        if (Talk("SWC CONFIG FACE PASSTHRU\r\n"))   printf("Passthru Ok!\r\n");
        SendXBMC("{\"jsonrpc\": \"2.0\", \"method\": \"GUI.ShowNotification\", \"params\":{\"title\":\"Carberry\",\"message\":\"Steeringwheel Commands OFF!\"}}");
        divert = false;
      }
    break;
  }*/
  return true;
}

/*-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
const tTermEvntTbl TermKeyKindClick =
{
	9,
	{
		{"VOLUM",  0, 0, (void*)TermKeyKindClickVolum,     0},
		{"VOLUP",  0, 0, (void*)TermKeyKindClickVolup,     0},
		{"SEEKM",  0, 0, (void*)TermKeyKindClickSeekm,     0},
		{"SEEKP",  0, 0, (void*)TermKeyKindClickSeekp,     0},
		{"SOURCE", 0, 0, (void*)TermKeyKindClickSource,    0},
		{"MUTE",   0, 0, (void*)TermKeyKindClickMute,      0},
		{"FACE",   0, 0, (void*)TermKeyKindClickFace,      0},
		{"ANSWER", 0, 0, (void*)TermKeyKindClickAnswer,    0},
		{"HANGUP", 0, 0, (void*)TermKeyKindClickHangup,    0},
	},
};

/*-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
const tTermEvntTbl TermKeyKindHold =
{
	9,
	{
		{"VOLUM",  0, 0, (void*)TermKeyKindHoldVolum,     0},
		{"VOLUP",  0, 0, (void*)TermKeyKindHoldVolup,     0},
		{"SEEKM",  0, 0, (void*)TermKeyKindHoldSeekm,     0},
		{"SEEKP",  0, 0, (void*)TermKeyKindHoldSeekp,     0},
		{"SOURCE", 0, 0, (void*)TermKeyKindHoldSource,    0},
		{"MUTE",   0, 0, (void*)TermKeyKindHoldMute,      0},
		{"FACE",   0, 0, (void*)TermKeyKindHoldFace,      0},
		{"ANSWER", 0, 0, (void*)TermKeyKindHoldAnswer,    0},
		{"HANGUP", 0, 0, (void*)TermKeyKindHoldHangup,    0},
	},
};

/*-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
const tTermEvntTbl TermKeyKind =
{
	3,
	{
//	{"PRESS",    0, 0, (void*)TermEvntDynamic,     (void*)&TermKeyKindPress},
		{"CLICK",    0, 0, (void*)TermEvntDynamic,     (void*)&TermKeyKindClick},
		{"HOLD",     0, 0, (void*)TermEvntDynamic,     (void*)&TermKeyKindHold},
//	{"RELEASE",  0, 0, (void*)TermEvntDynamic,     (void*)&TermKeyKindRelease},
		{"SEQUENCE", 0, 0, (void*)&TermKeySequence,        0},
	},
};

/*-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
const tTermEvntTbl TermEvents =
{
	2,
	{
		{"IGNITION" , 0, 0, (void*)TermCmdNimp,                           0},
		{"KEY"      , 0, 0, (void*)TermEvntDynamic,     (void*)&TermKeyKind},
	},
};

/*-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
const tTermEvntTbl TermEvntRoot =
{
	1,
	{
		{"EVNT", 0, 0, (void*)TermEvntDynamic, (void*)&TermEvents},
	},
};

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int ProcessEvents()
{
  char line[STDSTR];
  
  printf("Continue processing events...\r\n");

  while (true)
  {
    if (RecvSock(&sockfd, line, STDSTR, 1))
    {
      //Process received event
      char* pb = line;
      printf(line);
      TermEvntDynamic((void*)&TermEvntRoot, 0, 0, &pb);
    }
    else
    {
      //Try to reconnect
      if (!sockfd) CarberryConnect();
    }
  }

}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
void SetupCarberry()
{
  printf("Setup Carberry interface...\r\n");

  //if (Talk("VERSION\r\n")) printf("Version Ok!\r\n");
  if (Talk("AT\r\n")) printf("AT Ok!\r\n");

  //Enable notification
  if (Talk("CAN USER FILTER CH1 0 0206\r\n"))  printf("FILTRO INDEX 206 IMPOSTATO\r\n");
  if (Talk("CAN USER MASK CH1 0FFF\r\n"))  printf("MASCHERA xFFF IMPOSTATA \r\n");
  //if (Talk("CAN USER ALIGN RIGHT\r\n"))  printf("CAN ALLINEATO A DESTRA\r\n");
  if (Talk("CAN USER OPEN CH1 95K2\r\n"))  printf("CAN APERTO A 95K2\r\n");
  //if (Talk("SWC CONFIG SOURCE NOTIFY\r\n")) printf("Notify Ok!\r\n");
  //if (Talk("SWC CONFIG FACE NOTIFY\r\n"))   printf("Notify Ok!\r\n");
 }

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
void terminal_kill_handler(int signum)
{
  printf("\r\nTerminal kill signal handled\r\n");
  close(sockfd);
  close(xbmcfd);
  exit(0);
}

/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
  printf("Carberry CAN2DASH Control\r\n");

  //Kill hook
  if (signal(SIGINT,  terminal_kill_handler) == SIG_IGN) signal(SIGINT,  SIG_IGN);
  if (signal(SIGTERM, terminal_kill_handler) == SIG_IGN) signal(SIGTERM, SIG_IGN);

  CarberryConnect();

  SetupCarberry();
  ProcessEvents();

  return 0;
}
