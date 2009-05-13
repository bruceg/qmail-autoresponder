#include <sysdeps.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <fmt/multi.h>
#include <str/str.h>
#include "qmail-autoresponder.h"

const char usage_args[] = "[MESSAGE-FILE] DIRECTORY";
const char usage_post[] =
"  Temporary files are put into DIRECTORY track senders' rates.\n"
"  MESSAGE-FILE defaults to 'message.txt'.\n";

static const char* opt_msgfilename = "message.txt";
static const char* opt_logfilename = "log.txt";
static str tmpstr;

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

static void read_config(void)
{
  struct option* option;
  int fd;
  char buf[4096];
  size_t rd;
  char* end;
  
  for (option = options; option->name != 0; ++option) {
    /* Open and read all option files */
    if ((fd = open(option->name, O_RDONLY)) == -1) {
      if (errno == ENOENT)
	continue;
      fail_temp("System error opening configuration files.");
    }
    if ((rd = read(fd, buf, sizeof buf - 1)) == (size_t)-1)
      fail_temp("System error reading configuration files.");
    close(fd);
    /* Make sure it's NUL terminated and strip off all but the first line. */
    buf[rd] = 0;
    if ((end = strchr(buf, '\n')) != 0) {
      *end = 0;
      rd = end - buf;
    }
    /* Parse the data */
    option->copyfn(option->ptr, buf, rd);
  }
}

void init_autoresponder(int argc, char* argv[])
{
  int i = 0;
  switch (argc) {
  case 0: usage("Too few command-line arguments.");
  case 1: break;
  case 2: opt_msgfilename = argv[i++]; break;
  default: usage("Too many command-line arguments.");
  }
  if(chdir(argv[i]) == -1)
    usage("Could not change directory to DIRECTORY.");
  read_message(opt_msgfilename);
  read_config();
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
  if(fd == -1)
    fail_temp("Unable to create temporary file for sender.");
  close(fd);
}

int count_history(const char* sender)
{
  DIR* dir = opendir(".");
  direntry* entry;
  unsigned count = 0;
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
    if((unsigned long)(now - message_time) > opt_timelimit) {
      /* too old..ignore errors on unlink */
      if (!opt_nodelete)
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

  /* create the filename, format "PID.TIME.SENDER" */
  /* The PID is added to avoid collisions. */
  str_copyf(&tmpstr, "u\\.lu\\.s", getpid(), now, sender_copy);
  create_link(last_filename, tmpstr.s);
  return 1;
}

void log_sender(const char* sender, int responded)
{
  int fd;
  if ((fd = open(opt_logfilename, O_WRONLY | O_APPEND)) != -1) {
    str_copyf(&tmpstr, "u{ }cs{\n}", now, (responded ? '+' : '-'), sender);
    write(fd, tmpstr.s, tmpstr.len);
    close(fd);
  }
}
