/*------------------------------------------------------------------------------
------------------------------------------------------------------------------*/
#include <time.h>
#include <stdio.h>
#include <sys/time.h>

#define false 0
#define true  1

#define STDSTR 1024

typedef struct
{
  char text[32];
  int  option;
  int  dummy;
  int (*func)(void*, int pos, int idx, char** p);
  void*	param;
}
tTermEvntRow;

typedef struct
{
  unsigned int nrows;
  tTermEvntRow rows[];
}
tTermEvntTbl;
