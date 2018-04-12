#include <bglibs/sysdeps.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <bglibs/iobuf.h>
#include <bglibs/msg.h>
#include <bglibs/path.h>
#include <bglibs/str.h>
#include <bglibs/striter.h>
#include <bglibs/systime.h>
#include "qmail-autoresponder.h"

int opt_nodelete = 0;
int opt_nosend = 0;
int opt_quiet = 0;

time_t now;

const char program[] = "qmail-autoresponder";

str response = {0,0,0};
str tmpstr = {0,0,0};

void fail_msg(const char* msg)
{
  if(!opt_quiet && msg)
    msg1(msg);
}

void fail_temp(const char* msg)
{
  fail_msg(msg);
  exit(111);
}

void ignore(const char* msg)
{
  fail_msg("Ignoring message:");
  fail_msg(msg);
  exit(0);
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

static const char usage_str[] =
"{usage: }s{ [-cqDNT] [-n NUM] [-s STR] [-t TIME] [-O NAME=VALUE] }s{\n"
" -D       Don't remove old response records\n"
" -N       Don't send, just send autoresponse to standard output\n"
" -O N[=V] Set an extended option named N to value V (see below)\n"
"          If \"V\" is omitted, the value \"1\" (true) is used.\n"
" -c       Copy message into response\n"
" -n NUM   Set the maximum number of replies (defaults to 1)\n"
" -s STR   Add the subject to the autoresponse, prefixed by STR\n"
" -t TIME  Set the time interval, in seconds (defaults to 1 hour)\n"
" -q       Don't show error messages\n"
" If more than NUM messages are received from the same sender\n"
" within TIME seconds of each other, no response is sent.\n"
" If both -h and -H are specified, only -h is used.\n"
" This program must be run by qmail.\n"
"\n"
"Option names:\n"
"  copymsg        -- Copy the original message into the generated response\n"
"  headerkeep     -- List of headers to copy from the original message\n"
"  headerstrip    -- List of headers to remove from the original message\n"
"  msglimit       -- Set the maximum number replies per sender\n"
"  no_inreplyto   -- Do not add an In-Reply-To: header to the response\n"
"  numlines       -- Number of lines to copy from the original message\n"
"  separator      -- Add a separator between the response and copied message\n"
"  subject_prefix -- Add the original subject to the autoresponse with a prefix\n"
"  timelimit      -- Set the time interval, in seconds\n"
"  starttime      -- Set the time before which no response is sent\n"
"  endtime        -- Set the time after which no response is sent\n"
"Items within a list are seperated by \":\", and may contain wildcards.\n"
"\n"
"}s";

void usage(const char* msg)
{
  if(msg)
    msg1(msg);
  obuf_putf(&errbuf, usage_str, program, usage_args, usage_post);
  obuf_flush(&errbuf);
  exit(111);
}

static void parse_args(int argc, char* argv[])
{
  char* ptr;
  int ch;
  while((ch = getopt(argc, argv, "DNO:cn:qs:t:")) != -1) {
    switch(ch) {
    case 'c': opt_copymsg = 1; break;
    case 'n':
      opt_msglimit = strtoul(optarg, &ptr, 10);
      if(*ptr)
	usage("Invalid number for NUM.");
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
    case 'O':
      if ((ptr = strchr(optarg, '=')) == 0)
	handle_option(optarg, "1", 1);
      else {
	*ptr++ = 0;
	handle_option(optarg, ptr, strlen(ptr));
      }
      break;
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
  const char* recipient;
  const char* sender;
  
  parse_args(argc, argv);

  if (opt_starttime && now < opt_starttime)
    ignore("Autoresponder is not active yet");
  if (opt_endtime && now > opt_endtime)
    ignore("Autoresponder is no longer active");

  recipient = getenv("RECIPIENT");
  if(!recipient)
    usage("RECIPIENT is not set, must be run from qmail.");
  sender = getenv("SENDER");
  if(!sender)
    usage("SENDER is not set, must be run from qmail.");
  
  check_sender(sender);

  str_copys(&subject, "Your mail");

  read_parse_headers();

  // Check rate that SENDER has sent
  if(!count_history(sender)) {
    log_sender(sender, 0);
    ignore("SENDER has sent too many messages");
  }

  if(opt_nosend)
    out = &outbuf;
  else {
    int msgfd = qmail_start();
    if (!obuf_init(&bufout, msgfd, 0, IOBUF_NEEDSCLOSE, 0))
      fail_temp("Could not initialize output buffer.");
    out = &bufout;
  }
  
  obuf_put3s(out, "To: <", sender, ">\n");
  if(opt_subject_prefix)
    obuf_put4s(out, "Subject: ", opt_subject_prefix, subject.s, "\n");
  if((!opt_no_inreplyto) && (message_id.len != 0))
    obuf_put3s(out, "In-Reply-To: ", message_id.s, "\n");

  build_response(out, sender, recipient);

  if (!obuf_close(out))
    fail_temp("Could not close output.");
  
  if (!opt_nosend)
    qmail_finish(sender);
  log_sender(sender, 1);
  
  return 0;
}
