#include <bglibs/sysdeps.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <bglibs/fmt.h>
#include <bglibs/ibuf.h>
#include <bglibs/wrap.h>
#include <bglibs/str.h>
#include "qmail-autoresponder.h"

const char usage_args[] = "[MESSAGE-FILE] DIRECTORY";
const char usage_post[] =
"  Temporary files are put into DIRECTORY track senders' rates.\n"
"  MESSAGE-FILE defaults to 'message.txt'.\n";

static const char* opt_msgfilename = "message.txt";
static const char* opt_logfilename = "log.txt";
static const char* opt_configfilename = "config.txt";
static str safe_sender;
static const char* last_filename = 0;

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

static void read_config_files(void)
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

static int read_config_file(void)
{
  ibuf in;
  str line = {0};

  if (!ibuf_open(&in, opt_configfilename, 0)) {
    if (errno == ENOENT)
      return 0;
    fail_temp("Could not read config.txt");
  }
  while (ibuf_getstr(&in, &line, '\n')) {
    /* Strip just the trailing \n */
    str_truncate(&line, line.len - 1);
    if (line.len == 0)
      break;
    if (line.s[0] == '#')
      continue;
    /* Match lines like "key=value" */
    unsigned eq = str_findfirst(&line, '=');
    if ((int)eq > 0 && eq < line.len) {
      line.s[eq++] = 0;
      handle_option(line.s, line.s + eq, line.len - eq);
    }
  }
  str_free(&line);

  if (!ibuf_readall(&in, &response))
    fail_temp("Error while reading message from config file.");

  ibuf_close(&in);
  return 1;
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
  if (!read_config_file())
    read_config_files();
  if (response.len == 0)
    read_message(opt_msgfilename);
}

static void create_link(char* filename)
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

  /* Translate all '/' to ':', to avoid fake paths in email addresses */
  wrap_str(str_copys(&safe_sender, sender));
  str_subst(&safe_sender, '/', ':');

  /* check if there are too many responses in the logs */
  while((entry = readdir(dir)) != NULL) {
    char* end;
    time_t message_time;
    unsigned message_pid;
    if(entry->d_name[0] == '.')
      continue;
    message_time = strtoul(entry->d_name, &end, 10);
    if(message_time == 0 || end == NULL || *end != '.')
      continue;
    if((unsigned long)(now - message_time) > opt_timelimit) {
      /* too old..ignore errors on unlink */
      if (!opt_nodelete)
	unlink(entry->d_name);
    } else {
      message_pid = strtoul(end+1, &end, 10);
      if(message_pid == 0 || end == NULL || *end != '.')
        continue;
      if(strcasecmp(end+1, safe_sender.s)==0)
	/* If the user's count is already over the max,
	 * don't record any more. */
	if(++count >= opt_msglimit)
	  return 0;
      last_filename = entry->d_name;
    }
  }

  return 1;
}

void log_sender(const char* sender, int responded)
{
  int fd;

  if (responded) {
    /* create the filename, format "TIME.PID.SENDER" */
    /* The PID is added to avoid collisions. */
    str_copyf(&tmpstr, "lu\\.u\\.s", now, getpid(), safe_sender.s);
    create_link(tmpstr.s);
  }

  if ((fd = open(opt_logfilename, O_WRONLY | O_APPEND)) != -1) {
    str_copyf(&tmpstr, "u{ }cs{\n}", now, (responded ? '+' : '-'), sender);
    if (write(fd, tmpstr.s, tmpstr.len) != tmpstr.len) { /* ignore */ }
    close(fd);
  }
  (void)sender;
}
