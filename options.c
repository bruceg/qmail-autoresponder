#define _XOPEN_SOURCE
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <bglibs/msg.h>
#include <bglibs/str.h>
#include "qmail-autoresponder.h"

int opt_copymsg = 0;
int opt_no_inreplyto = 0;

unsigned long opt_timelimit = 3600;
unsigned long opt_msglimit = 1;
unsigned long opt_numlines = ~0UL;

const char* opt_subject_prefix = 0;
const char* opt_headerkeep = 0;
const char* opt_headerstrip = 0;
const char* opt_separator = 0;
const char* opt_bcc = 0;

time_t opt_starttime = 0;
time_t opt_endtime = 0;

static const char* copy_bool(void* ptr, const char* value, unsigned int length)
{
  int* dest = ptr;
  switch (value[0]) {
  case '1' ... '9':
  case 'T':
  case 't':
  case 'y':
  case 'Y':
    *dest = 1;
    return NULL;
  case '0':
  case 'F':
  case 'f':
  case 'N':
  case 'n':
    *dest = 0;
    return NULL;
  default:
    return "Invalid boolean";
  }
  (void)length;
}

static const char* copy_ulong(void* ptr, const char* value, unsigned int length)
{
  unsigned long* dest = ptr;
  char* end;
  *dest = strtoul(value, &end, 10);
  return (end < value + length) ? "Invalid number" : NULL;
  (void)length;
}

static const char* copy_str(void* ptr, const char* value, unsigned int length)
{
  char** dest = ptr;
  *dest = malloc(length + 1);
  memcpy(*dest, value, length);
  (*dest)[length] = 0;
  return NULL;
}

static const char* time_formats[] = {
  "%Y-%m-%d",
  "%Y-%m-%d %H:%M:%S",
  "%c",
};

static const char* copy_time(void* ptr, const char* value, unsigned int length)
{
  time_t* dest = ptr;
  unsigned long t;
  char* end;
  unsigned int i;
  if ((t = strtoul(value, &end, 10)) > 0 && *end == 0) {
    *dest = t;
    return NULL;
  }
  /* MySQL default non-NULL time value is all zeros */
  if (strcmp(value, "0000-00-00 00:00:00") == 0) {
    *dest = 0;
    return NULL;
  }
  for (i = 0; i < sizeof time_formats / sizeof *time_formats; i++) {
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    if (strptime(value, time_formats[i], &tm) == value + length) {
      if ((*dest = mktime(&tm)) != (time_t)-1)
        return NULL;
    }
  }
  return "Invalid time value";
}

struct option options[] = {
  { "copymsg", &opt_copymsg, copy_bool },
  { "no_inreplyto", &opt_no_inreplyto, copy_bool },

  { "msglimit", &opt_msglimit, copy_ulong },
  { "numlines", &opt_numlines, copy_ulong },
  { "timelimit", &opt_timelimit, copy_ulong },

  { "headerkeep", &opt_headerkeep, copy_str },
  { "headerstrip", &opt_headerstrip, copy_str },
  { "separator", &opt_separator, copy_str },
  { "subject_prefix", &opt_subject_prefix, copy_str },
  { "bcc", &opt_bcc, copy_str },

  { "starttime", &opt_starttime, copy_time },
  { "endtime", &opt_endtime, copy_time },

  { 0, 0, 0 }
};

void handle_option(const char* name,
		   const char* value,
		   unsigned int length)
{
  struct option* option;
  const char* err;
  for (option = options; option->name != 0; ++option) {
    if (strcmp(name, option->name) == 0) {
      if ((err = option->copyfn(option->ptr, value, length)) != NULL) {
        if (!opt_quiet)
          errorf("{Configuration error in }s{: }s{: \"}s{\"}", name, err, value);
        exit(111);
      }
      break;
    }
  }
}
