#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <iobuf/iobuf.h>
#include <systime.h>
#include <sysdeps.h>
#include "qmail-autoresponder.h"

int opt_quiet = 0;
int opt_copymsg = 0;
int opt_nodelete = 0;
int opt_nosend = 0;
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
static str subject;
static str message_id;

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

static void ignore_ml(const char* s, const char* header)
{
  unsigned hdrlen = strlen(header);
  if (strncasecmp(s, header, hdrlen) == 0 && s[hdrlen] == ':') {
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

static const char* skip_space(const char* s)
{
  while(*s && isspace(*s))
    ++s;
  return s;
}

static char header[8192];
static ssize_t headersize;

static void parse_header(const char* s, unsigned length)
{
  ignore_ml(s, "List-ID");
  ignore_ml(s, "Mailing-List");
  ignore_ml(s, "X-Mailing-List");
  ignore_ml(s, "X-ML-Name");
  ignore_ml(s, "List-Help");
  ignore_ml(s, "List-Unsubscribe");
  ignore_ml(s, "List-Subscribe");
  ignore_ml(s, "List-Post");
  ignore_ml(s, "List-Owner");
  ignore_ml(s, "List-Archive");
  
  if(!strncasecmp(s, "Precedence:", 11)) {
    const char* start = skip_space(s + 11);
    const char* end = start;
    while(end < s+length && !isspace(*end))
      ++end;
    if(!strncasecmp(start, "junk", end-start) ||
       !strncasecmp(start, "bulk", end-start) ||
       !strncasecmp(start, "list", end-start))
      ignore("Message has a junk, bulk, or list precedence header");
  }
  else if(!strncasecmp(s, dtline, dtline_len-1))
    ignore("Message already has my Delivered-To line");
  else if(!strncasecmp(s, "Subject:", 8)) {
    str_copys(&subject, s+8);
    str_strip(&subject);
  }
  else if(!strncasecmp(s, "Message-ID:", 11)) {
    str_copys(&message_id, skip_space(s+11));
    str_strip(&message_id);
  }
}

static void parse_headers(void)
{
  char* ptr = header;
  const char* headerend;
  
  if(lseek(0, 0, SEEK_SET) == -1)
    fail_temp("Could not rewind input message.");
  
  /* Only read in a fixed number of bytes */
  headersize = read(0, header, sizeof(header));
  if(headersize == 0 || headersize == -1)
    fail_temp("Could not read header.");
  
  headerend = header + headersize;
  
  /* Find the start and end of header lines */
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

static void exec_qmail_inject(int fd)
{
  putenv("QMAILNAME=");
  putenv("QMAILUSER=");
  putenv("QMAILHOST=");
  close(0);
  dup2(fd, 0);
  close(fd);
  execl("/var/qmail/bin/qmail-inject", "/var/qmail/bin/qmail-inject",
	"-h", "-f", "", 0);
  fail_temp("Could not exec qmail-inject");
}

static int popen_inject()
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
    exec_qmail_inject(fds[0]);
    break;
  }
  close(fds[0]);
  return fds[1];
}

static void pclose_inject(void)
{
  int status;
  if(waitpid(inject_pid, &status, WUNTRACED) == -1)
    fail_temp("Failed to catch exit status of qmail-inject");
  if(!WIFEXITED(status))
    fail_temp("qmail-inject crashed");
  if(WEXITSTATUS(status))
    fail_temp("qmail-inject failed");
}

static void write_response(obuf* out)
{
  const char* next;
  const char* buf = response.s;
  long len = response.len;

  while (len > 0 && (next = memchr(buf, '%', len)) != 0) {
    long todo;
    todo = next-buf;
    obuf_write(out, buf, todo);
    len -= todo;
    buf += todo;
    if (len > 1) {
      ++buf; --len;
      switch (*buf) {
      case 'S':
	obuf_putstr(out, &subject);
	++buf; --len;
	break;
      default:
	obuf_putc(out, '%');
      }
    }
    else
      break;
  }
  if (len > 0)
    obuf_write(out, buf, len);
}
    
static void copy_input(obuf* out)
{
  long rd;
  char buf[4096];
  obuf_write(out, header, headersize);
  while ((rd = read(0, buf, 4096)) > 0)
    obuf_write(out, buf, rd);
}

static const char* usage_str =
"usage: %s [-cqDNT] [-n NUM] [-s STR] [-t TIME] %s\n"
" -c       Copy message into response\n"
" -n NUM   Set the maximum number of replies (defaults to 1)\n"
" -s STR   Add the subject to the autoresponse, prefixed by STR\n"
" -t TIME  Set the time interval, in seconds (defaults to 1 hour)\n"
" -q       Don't show error messages\n"
" -D       Don't remove old response records\n"
" -N       Don't send, just send autoresponse to standard output\n"
" If more than NUM messages are received from the same sender\n"
" within TIME seconds of each other, no response is sent.\n"
" This program must be run by qmail.\n"
"%s";

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
  while((ch = getopt(argc, argv, "cn:qs:t:DN")) != EOF) {
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
    default:
      usage(0);
    }
  }
  init_autoresponder(argc-optind, argv+optind);
  now = time(0);
}

int main(int argc, char* argv[])
{
  obuf bufout;
  obuf* out;
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

  str_copys(&subject, "Your mail");
  // Read and parse header
  parse_headers();

  // Check rate that SENDER has sent
  if(!count_history(sender))
    ignore("SENDER has sent too many messages");

  if(opt_nosend)
    out = &outbuf;
  else {
    int fd = popen_inject();
    if (!obuf_init(&bufout, fd, 0, IOBUF_NEEDSCLOSE, 0))
      fail_temp("Could not initialize output buffer.");
    out = &bufout;
  }
  
  obuf_put3s(out, "To: <", sender, ">\n");
  if(opt_subject_prefix)
    obuf_put4s(out, "Subject: ", opt_subject_prefix, subject.s, "\n");
  if((!opt_no_inreplyto) && (message_id.len != 0))
    obuf_put3s(out, "In-Reply-To: ", message_id.s, "\n");

  write_response(out);
  
  if(opt_copymsg)
    copy_input(out);

  if (!obuf_close(out))
    fail_temp("Could not close output.");
  
  if (!opt_nosend)
    pclose_inject();
  
  return 0;
}
