#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "systime.h"
#include "fork.h"
#include "qmail-autoresponder.h"

int opt_quiet = 0;
int opt_copymsg = 0;
int opt_nodelete = 0;
int opt_nosend = 0;
int opt_notoline = 0;
int opt_no_inreplyto = 0;
time_t opt_timelimit = 3600;
unsigned opt_msglimit = 1;
const char* opt_subject_prefix = 0;

const char* argv0;

time_t now;

str response = {0,0,0};

static const char* dtline;
static size_t dtline_len;
static pid_t inject_pid;
static const char* subject = "Your mail";
static ssize_t subject_len = 9;
static const char* message_id = NULL;
static ssize_t message_id_len = 0;

void fail_msg(const char* msg)
{
  if(!opt_quiet && msg)
    fprintf(stderr, "%s: %s\n", argv0, msg);
}

void fail_temp(const char* msg)
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

static void ignore_ml(const char* str, const char* header)
{
  unsigned hdrlen = strlen(header);
  if (strncasecmp(str, header, hdrlen) == 0 && str[hdrlen] == ':') {
    fail_msg("Ignoring message:");
    fprintf(stderr, "%s: %s (%s header)\n", argv0,
	    "Message appears to be from a mailing list", header);
    exit(0);
  }
}

static void check_sender(const char* sender)
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

static const char* skip_space(const char* str)
{
  while(*str && isspace(*str))
    ++str;
  return str;
}

static char header[8192];
static ssize_t headersize;

static void parse_header(const char* str, unsigned length)
{
  ignore_ml(str, "List-ID");
  ignore_ml(str, "Mailing-List");
  ignore_ml(str, "X-Mailing-List");
  ignore_ml(str, "X-ML-Name");
  ignore_ml(str, "List-Help");
  ignore_ml(str, "List-Unsubscribe");
  ignore_ml(str, "List-Subscribe");
  ignore_ml(str, "List-Post");
  ignore_ml(str, "List-Owner");
  ignore_ml(str, "List-Archive");
  
  if(!strncasecmp(str, "Precedence:", 11)) {
    const char* start = skip_space(str + 11);
    const char* end = start;
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
    subject = skip_space(str + 8);
    subject_len = strlen(subject);
  }
  else if(!strncasecmp(str, "Message-ID:", 11)) {
    message_id = skip_space(str + 11);
    message_id_len = strlen(message_id);
  }
}

static void parse_headers(void)
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
	  char tmp = ptr[-1];
	  ptr[-1] = 0;
	  parse_header(start, ptr-start-1);
	  ptr[-1] = tmp;
	  break;
	}
      }
    }
  }

  /* Ignore messages with oversize headers */
  if(ptr >= header + sizeof(header))
    ignore("Header of received message was too long");
}

static void exec_qmail_inject(const char* sender, int fd)
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

static int popen_inject(const char* sender)
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

static void pclose_inject(int fdout)
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

static void parse_write_block(int fdout, const char* buf, long len)
{
  const char* next;
  while (len > 0 && (next = memchr(buf, '%', len)) != 0) {
    long todo;
    todo = next-buf;
    if (write(fdout, buf, todo) != todo)
      fail_temp("Could not write to output in parse_write_block");
    len -= todo;
    buf += todo;
    if (len > 1) {
      ++buf; --len;
      switch (*buf) {
      case 'S':
	if (write(fdout, subject, subject_len) != subject_len)
	  fail_temp("Could not write subject in parse_write_block");
	++buf; --len;
	break;
      default:
	write(fdout, "%", 1);
      }
    }
    else
      break;
  }
  if (len > 0)
    if (write(fdout, buf, len) != len)
      fail_temp("Could not write to output in parse_write_block");
}
    
static void copy_input(int fdout)
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

static const char* usage_str =
"usage: %s [-cqDNT] [-n NUM] [-s STR] [-t TIME] %s
  -c       Copy message into response
  -n NUM   Set the maximum number of replies (defaults to 1)
  -s STR   Add the subject to the autoresponse, prefixed by STR
  -t TIME  Set the time interval, in seconds (defaults to 1 hour)
  -q       Don't show error messages
  -D       Don't remove old response records
  -N       Don't send, just send autoresponse to standard output
  -T       Don't add a 'To: <SENDER>' line to the response
  If more than NUM messages are received from the same sender
  within TIME seconds of each other, no response is sent.
  This program must be run by qmail.
%s";

void usage(const char* msg)
{
  if(msg)
    fprintf(stderr, "%s: %s\n", argv0, msg);
  fprintf(stderr, usage_str, argv0, usage_args, usage_post);
  exit(111);
}

static void parse_args(int argc, char* argv[])
{
  char* ptr;
  int ch;
  argv0 = argv[0];
  while((ch = getopt(argc, argv, "cn:qs:t:DNT")) != EOF) {
    switch(ch) {
    case 'c': opt_copymsg = 1; break;
    case 'n':
      opt_msglimit = strtoul(optarg, &ptr, 10);
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
    case 'D': opt_nodelete = 1;  break;
    case 'N': opt_nosend = 1;    break;
    case 'T': opt_notoline = 1;  break;
    default:
      usage(0);
    }
  }
  init_autoresponder(argc-optind, argv+optind);
  now = time(0);
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
  if(!count_history(sender))
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
  if((!opt_no_inreplyto) && (message_id_len != 0)) {
    write(fdout, "In-Reply-To: ", 13);
    write(fdout, message_id, message_id_len);
    write(fdout, "\n", 1);
  }

  parse_write_block(fdout, response.s, response.len);
  
  if(opt_copymsg)
    copy_input(fdout);

  if(opt_nosend)
    close(1);
  else
    pclose_inject(fdout);
  
  return 0;
}
