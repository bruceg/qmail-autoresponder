#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int opt_quiet = 0;
static int opt_copyinput = 0;
static int opt_nosend = 0;
static int opt_nolinks = 0;
static int opt_notoline = 0;
static time_t opt_timelimit = 3600;
static unsigned opt_maxmsgs = 1;
static const char* opt_subject_prefix = 0;

static int opt_msgfile;
static const char* argv0;

static time_t now;
static const char* dtline;
static size_t dtline_len;
static pid_t inject_pid;
static const char* subject = "Your mail";
static ssize_t subject_len = 9;

static void fail_msg(const char* msg)
{
  if(!opt_quiet && msg)
    fprintf(stderr, "%s: %s\n", argv0, msg);
}

static void fail_temp(const char* msg)
{
  fail_msg(msg);
  exit(111);
}

static void ignore(const char* msg)
{
  fail_msg("Ignoring message:");
  fail_msg(msg);
  exit(0);
}

static const char* usage_str =
"usage: %s [-cqLNT] [-n NUM] [-s STR] [-t TIME] MESSAGE-FILE DIRECTORY
  -c       Copy message into response
  -n NUM   Set the maximum number of replies (defaults to 1)
  -s STR   Add the subject to the autoresponse, prefixed by STR
  -t TIME  Set the time interval, in seconds (defaults to 1 hour)
  -q       Don't show error messages
  -L       Don't try to make links to conserve inodes
  -N       Don't send, just send autoresponse to standard output
  -T       Don't add a 'To: <SENDER>' line to the response
  Temporary files are put into DIRECTORY track senders' rates.
  If more than NUM messages are received from the same sender
  within TIME seconds of each other, no response is sent.
  This program must be run by qmail.
";

static void usage(const char* msg)
{
  if(msg)
    fprintf(stderr, "%s: %s\n", argv0, msg);
  fprintf(stderr, usage_str, argv0);
  exit(111);
}

void parse_args(int argc, char* argv[])
{
  char* ptr;
  int ch;
  argv0 = argv[0];
  while((ch = getopt(argc, argv, "cn:qs:t:LNT")) != EOF) {
    switch(ch) {
    case 'c': opt_copyinput = 1; break;
    case 'n':
      opt_maxmsgs = strtoul(optarg, &ptr, 10);
      if(*ptr)
	usage("Invalid number for NUL.");
      break;
    case 'q': opt_quiet = 1;   break;
    case 's': opt_subject_prefix = optarg; break;
    case 't':
      opt_timelimit = strtoul(optarg, &ptr, 10);
      if(*ptr)
	usage("Invalid number for TIME.");
      break;
    case 'L': opt_nolinks = 1;   break;
    case 'N': opt_nosend = 1;    break;
    case 'T': opt_notoline = 1;  break;
    default:
      usage(0);
    }
  }
  if(argc - optind < 2)
    usage("Too few command-line arguments.");
  if(argc - optind > 2)
    usage("Too many command-line arguments.");
  opt_msgfile = open(argv[optind], O_RDONLY);
  if(opt_msgfile == -1)
    usage("Could not open message file.");
  if(chdir(argv[optind+1]) == -1)
    usage("Could not change directory to DIRECTORY.");
  now = time(0);
}

void check_sender(const char* sender)
{
  // Ignore messages with an empty SENDER (sent from system)
  if(!sender[0])
    ignore("SENDER is empty, mail came from system account");
  if(!strcmp(sender, "#@[]"))
    ignore("SENDER is <#@[]> (double bounce message)");
  if(!strchr(sender, '@'))
    ignore("SENDER did not contain a hostname");
  if(!strncasecmp(sender, "mailer-daemon", 13))
    ignore("SENDER was mailer-daemon");
}

static char header[8192];
static ssize_t headersize;

void parse_header(const char* str, unsigned length)
{
  if(!strncasecmp(str, "List-ID:", 8))
    ignore("Message appears to be from a mailing list (List-ID header)");
  else if(!strncasecmp(str, "Mailing-List:", 13))
    ignore("Message appears to be from a mailing list (Mailing-List header)");
  else if(!strncasecmp(str, "X-Mailing-List:", 15))
    ignore("Message appears to be from a mailing list (X-Mailing-List header)");
  else if(!strncasecmp(str, "X-ML-Name:", 10))
    ignore("Message appears to be from a mailing list (X-ML-Name header)");
  else if(!strncasecmp(str, "Precedence:", 11)) {
    const char* start = str + 11;
    const char* end;
    while(start < str+length && isspace(*start))
      ++start;
    end = start;
    while(end < str+length && !isspace(*end))
      ++end;
    if(!strncasecmp(start, "junk", end-start) ||
       !strncasecmp(start, "bulk", end-start) ||
       !strncasecmp(start, "list", end-start))
      ignore("Message has a junk, bulk, or list precedence header");
  }
  else if(!strncasecmp(str, dtline, dtline_len-1))
    ignore("Message already has my Delivered-To line");
  else if(!strncasecmp(str, "Subject:", 8)) {
    subject = str + 8;
    while(*subject && isspace(*subject))
      ++subject;
    subject_len = strlen(subject);
  }
}

void parse_headers(void)
{
  char* ptr = header;
  const char* headerend;
  const char* start;
  
  if(lseek(0, 0, SEEK_SET) == -1)
    fail_temp("Could not rewind input message.");
  
  /* Only read in a fixed number of bytes */
  headersize = read(0, header, sizeof(header));
  if(headersize == 0 || headersize == -1)
    fail_temp("Could not read header.");
  
  headerend = header + headersize;
  
  /* Find the start and end of header lines */
  start = ptr;
  while(ptr < headerend) {
    const char* start = ptr;

    /* The headers end with a blank line */
    if(*ptr == '\n')
      break;
    
    while(ptr < headerend) {
      if(*ptr++ == '\n') {
	if(ptr >= headerend || *ptr == '\n' || !isspace(*ptr)) {
	  ptr[-1] = 0;
	  parse_header(start, ptr-start-1);
	  break;
	}
      }
    }
  }

  /* Ignore messages with oversize headers */
  if(ptr >= header + sizeof(header))
    ignore("Header of received message was too long");
}

void exec_qmail_inject(const char* sender, int fd)
{
  putenv("QMAILNAME=");
  putenv("QMAILUSER=");
  putenv("QMAILHOST=");
  close(0);
  dup2(fd, 0);
  close(fd);
  execl("/var/qmail/bin/qmail-inject", "/var/qmail/bin/qmail-inject",
	"-a", "-f", "", sender, 0);
  fail_temp("Could not exec qmail-inject");
}

int popen_inject(const char* sender)
{
  int fds[2];
  if(pipe(fds) == -1)
    fail_temp("Could not create pipe");
  
  inject_pid = fork();
  switch(inject_pid) {
  case -1:
    fail_temp("Could not fork");
    break;
  case 0:
    close(fds[1]);
    exec_qmail_inject(sender, fds[0]);
    break;
  }
  close(fds[0]);
  return fds[1];
}

void pclose_inject(int fdout)
{
  int status;
  close(fdout);
  if(waitpid(inject_pid, &status, WUNTRACED) == -1)
    fail_temp("Failed to catch exit status of qmail-inject");
  if(!WIFEXITED(status))
    fail_temp("qmail-inject crashed");
  if(WEXITSTATUS(status))
    fail_temp("qmail-inject failed");
}

void parse_write_block(int fdout, const char* buf, size_t len)
{
  static int saw_percent = 0;
  while(len > 0) {
    ssize_t todo;
    ssize_t incr;
    char* next;
    if(saw_percent) {
      write(2, "Delayed percent\n", 16);
      saw_percent = 0;
      switch(*buf) {
      case 'S':
	if(write(fdout, subject, subject_len) != subject_len)
	  fail_temp("Could not write subject in parse_write_block");
	++buf;
	--len;
	continue;
      default:
	write(fdout, "%", 1);
	break;
      }
    }
    next = memchr(buf, '%', len);
    if(next) {
      todo = next - buf;
      incr = todo + 1;
      saw_percent = 1;
    }
    else
      incr = todo = len;
    if(write(fdout, buf, todo) != todo)
      fail_temp("Could not write to output in parse_write_block");
    len -= incr;
    buf += incr;
  }
}
    
void copy_input(int fdout)
{
  ssize_t rd;
  char buf[4096];
  if(write(fdout, header, headersize) != headersize)
    fail_temp("Could not write header in copy_input");
  while((rd = read(0, buf, 4096)) > 0) {
    ssize_t wr = write(fdout, buf, rd);
    if(wr != rd)
      fail_temp("Could not write to output in copy_input");
  }
}

void copy_msgfile(int fdin, int fdout)
{
  ssize_t rd;
  char buf[4096];
  while((rd = read(fdin, buf, 4096)) > 0)
    parse_write_block(fdout, buf, rd);
}

int count_history(const char* sender, unsigned max)
{
  DIR* dir = opendir(".");
  struct dirent* entry;
  unsigned count = 0;
  char* filename;
  int fd;
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
	if(++count >= max)
	  return 0;
      last_filename = entry->d_name;
    }
  }

  /* Conserve inodes -- create links when possible */
  if(last_filename && !opt_nolinks) {
    if(link(last_filename, filename) == -1)
      fail_temp("Could not create link for sender");
  }
  /* Otherwise, create a new 0-byte file now */
  else {
    fd = open(filename, O_WRONLY|O_CREAT|O_EXCL, 0444);
    free(filename);
    if(fd == -1)
      fail_temp("Unable to create temporary file for sender.");
    close(fd);
  }
  
  return 1;
}

int main(int argc, char* argv[])
{
  int fdout;
  const char* sender;
  
  parse_args(argc, argv);

  // Fail if SENDER or DTLINE are not set
  sender = getenv("SENDER");
  if(!sender)
    usage("SENDER is not set, must be run from qmail.");
  dtline = getenv("DTLINE");
  if(!dtline)
    usage("DTLINE is not set; must be run from qmail.");
  dtline_len = strlen(dtline);
  
  check_sender(sender);

  // Read and parse header
  parse_headers();

  // Check rate that SENDER has sent
  if(!count_history(sender, opt_maxmsgs))
    ignore("SENDER has sent too many messages");

  if(opt_nosend)
    fdout = 1;
  else
    fdout = popen_inject(sender);

  write(fdout, dtline, strlen(dtline));
  if(!opt_notoline) {
    write(fdout, "To: <", 5);
    write(fdout, sender, strlen(sender));
    write(fdout, ">\n", 2);
  }
  if(opt_subject_prefix) {
    write(fdout, "Subject: ", 9);
    write(fdout, opt_subject_prefix, strlen(opt_subject_prefix));
    write(fdout, subject, subject_len);
    write(fdout, "\n", 1);
  }
  copy_msgfile(opt_msgfile, fdout);
  close(opt_msgfile);
  
  if(opt_copyinput)
    copy_input(fdout);

  if(opt_nosend)
    close(1);
  else
    pclose_inject(fdout);
  
  return 0;
}
