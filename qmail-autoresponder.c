#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <str/str.h>
#include <sysdeps.h>
#include "qmail-autoresponder.h"

const char usage_args[] = "[MESSAGE-FILE] DIRECTORY";
const char usage_post[] =
"  Temporary files are put into DIRECTORY track senders' rates.\n"
"  MESSAGE-FILE defaults to 'message.txt'.\n";

static const char* opt_msgfilename = "message.txt";

static void read_message(const char* filename)
{
  int fd;
  long rd;
  char buf[4096];
  if ((fd = open(filename, O_RDONLY)) == -1)
    usage("Could not open message file.");
  while ((rd = read(fd, buf, sizeof buf)) > 0)
    if (!str_catb(&response, buf, rd))
      fail_temp("Out of memory while reading in message file.");
  close(fd);
}

void init_autoresponder(int argc, char* argv[])
{
  int optind = 0;
  switch (argc) {
  case 0: usage("Too few command-line arguments.");
  case 1: break;
  case 2: opt_msgfilename = argv[optind++]; break;
  default: usage("Too many command-line arguments.");
  }
  if(chdir(argv[optind]) == -1)
    usage("Could not change directory to DIRECTORY.");
  read_message(opt_msgfilename);
}

static void create_link(const char* last_filename, char* filename)
{
  int fd;
  
  /* Conserve inodes -- create links when possible */
  if(last_filename)
    if(link(last_filename, filename) == 0)
      return;

  /* Otherwise, create a new 0-byte file now */
  fd = open(filename, O_WRONLY|O_CREAT|O_EXCL, 0444);
  free(filename);
  if(fd == -1)
    fail_temp("Unable to create temporary file for sender.");
  close(fd);
}

int count_history(const char* sender)
{
  DIR* dir = opendir(".");
  direntry* entry;
  unsigned count = 0;
  char* filename;
  size_t sender_len;
  char* sender_copy;
  size_t i;
  char* last_filename = 0;

  /* Translate all '/' to ':', to avoid fake paths in email addresses */
  sender_len = strlen(sender);
  sender_copy = malloc(sender_len+1);
  for(i = 0; i < sender_len; i++)
    sender_copy[i] = (sender[i] == '/') ? ':' : sender[i];
  sender_copy[i] = 0;

  /* create the filename, format "PID.TIME.SENDER" */
  /* The PID is added to avoid collisions. */
  filename = malloc(sender_len+100);
  sprintf(filename, "%u.%lu.%s", getpid(), now, sender_copy);

  /* check if there are too many responses in the logs */
  while((entry = readdir(dir)) != NULL) {
    char* ptr;
    char* end;
    time_t message_time;
    unsigned message_pid;
    if(entry->d_name[0] == '.')
      continue;
    message_pid = strtoul(entry->d_name, &ptr, 10);
    if(message_pid == 0 || *ptr != '.')
      continue;
    message_time = strtoul(ptr+1, &end, 10);
    if(!end || *end != '.')
      continue;
    if(now-message_time > opt_timelimit) {
      /* too old..ignore errors on unlink */
      unlink(entry->d_name);
    } else {
      if(strcasecmp(end+1, sender_copy)==0)
	/* If the user's count is already over the max,
	 * don't record any more. */
	if(++count >= opt_msglimit)
	  return 0;
      last_filename = entry->d_name;
    }
  }

  create_link(last_filename, filename);
  return 1;
}
