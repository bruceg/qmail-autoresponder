#include <mysql/mysql.h>
#include <stdlib.h>
#include <string.h>
#include <str/str.h>
#include "qmail-autoresponder.h"

int opt_quiet = 0;
int opt_copymsg = 0;
int opt_nodelete = 0;
int opt_nosend = 0;
int opt_no_inreplyto = 0;
unsigned long opt_timelimit = 3600;
unsigned long opt_msglimit = 1;
unsigned long opt_numlines = ~0UL;
const char* opt_subject_prefix = 0;
const char* opt_headerkeep = 0;
const char* opt_headerstrip = 0;

static void copy_bool(void* ptr, const char* value, unsigned int length)
{
  int* dest = ptr;
  switch (value[0]) {
  case '1' ... '9':
    *dest = 1;
    break;
  case '0':
    *dest = 0;
    break;
  }
  (void)length;
}

static void copy_ulong(void* ptr, const char* value, unsigned int length)
{
  unsigned long* dest = ptr;
  *dest = strtoul(value, 0, 10);
  (void)length;
}

static void copy_str(void* ptr, const char* value, unsigned int length)
{
  char** dest = ptr;
  *dest = malloc(length + 1);
  memcpy(*dest, value, length);
  (*dest)[length] = 0;
}

struct option options[] = {
  { "timelimit", &opt_timelimit, copy_ulong },
  { "msglimit", &opt_msglimit, copy_ulong },
  { "copymsg", &opt_copymsg, copy_bool },
  { "subject_prefix", &opt_subject_prefix, copy_str },
  { "headerkeep", &opt_headerkeep, copy_str },
  { "headerstrip", &opt_headerstrip, copy_str },
  { 0, 0, 0 }
};
