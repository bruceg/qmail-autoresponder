#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <iobuf/iobuf.h>
#include <str/str.h>
#include <str/iter.h>
#include <systime.h>
#include <sysdeps.h>
#include "qmail-autoresponder.h"

const char* argv0;

time_t now;

str response = {0,0,0};

static const char* dtline;
static size_t dtline_len;
static pid_t inject_pid;
static str subject;
static str message_id;
static str content_type;
static str boundary;
static str tmpstr;

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

static void ignore_ml(const str* s, const char* header)
{
  unsigned hdrlen = strlen(header);
  if (strncasecmp(s->s, header, hdrlen) == 0 && s->s[hdrlen] == ':') {
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

static void header_copy_if(const str* src, str* dest,
			   const char* prefix, unsigned prefix_len)
{
  unsigned int lf;
  unsigned int end;

  if (src->len > prefix_len
      && strncasecmp(src->s, prefix, prefix_len) == 0) {
    str_copyb(dest, src->s + prefix_len, src->len - prefix_len);
    str_strip(dest);
    /* Replace embeded newlines followed by variable whitespace with a
     * single space. */
    while ((lf = str_findfirst(dest, '\n')) < dest->len) {
      end = lf + 1;
      while (lf > 0 && isspace(dest->s[lf-1]))
	--lf;
      while (end < dest->len && isspace(dest->s[end]))
	++end;
      str_spliceb(dest, lf, end - lf, " ", 1);
    }
  }
}

static int header_test(const str* h, const char* list)
{
  static str pattern;
  const char* colon;
  size_t len;

  do {
    len = ((colon = strchr(list, ':')) == 0)
      ? strlen(list)
      : (size_t)(colon - list);
    str_copyb(&pattern, list, len);
    str_cats(&pattern, ": *");
    if (str_case_glob(h, &pattern))
      return 1;
    list = colon + 1;
  } while (colon != 0);
  return 0;
}

static str copyheaders;
static str headers;

static void parse_header(const str* s)
{
  if (opt_copymsg) {
    if (opt_headerkeep
	? header_test(s, opt_headerkeep)
	: opt_headerstrip
	? !header_test(s, opt_headerstrip)
	: 1)
      str_cat(&copyheaders, s);
  }

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
  
  if(!strncasecmp(s->s, "Precedence:", 11)) {
    const char* start = skip_space(s->s + 11);
    const char* end = start;
    while (end < s->s + s->len && !isspace(*end))
      ++end;
    if(!strncasecmp(start, "junk", end-start) ||
       !strncasecmp(start, "bulk", end-start) ||
       !strncasecmp(start, "list", end-start))
      ignore("Message has a junk, bulk, or list precedence header");
  }
  else if(!strncasecmp(s->s, dtline, dtline_len-1))
    ignore("Message already has my Delivered-To line");
  else {
    header_copy_if(s, &subject, "subject:", 8);
    header_copy_if(s, &message_id, "message-id:", 11);
    header_copy_if(s, &content_type, "content-type:", 13);
  }
}

static void read_headers(void)
{
  str_truncate(&headers, 0);
  while (ibuf_getstr(&inbuf, &tmpstr, LF)) {
    if (tmpstr.s[0] == LF)
      break;
    str_cat(&headers, &tmpstr);
  }
}

static const char* find_field(const char* s, const char* field)
{
  int fpos;
  for (fpos = -1; *s != 0; ++s) {
    if (isspace(*s) || *s == ';')
      fpos = 0;
    else if (fpos >= 0) {
      if (tolower(*s) != field[fpos])
        fpos = -1;
      else if (field[++fpos] == 0) {
        do { ++s; } while(isspace(*s));
        if (*s != '=')
          fpos = -1;
        else {
          do { ++s; } while(isspace(*s));
          return s;
        }
      }
    }
  }
  return 0;
}

static int extract_field(const char* s, const char* field, str* value)
{
  char quote;
  const char* start;
  const char* end;
  
  if ((start = find_field(s, field)) != 0) {
    if (*start == '"' || *start == '\'')
      for (quote = *start++, end = start; *end != 0 && *end != quote; ++end)
	;
    else
      for (end = start; *end != 0 && !isspace(*end) && (*end != ';'); ++end)
	;
    str_copyb(value, start, end - start);
    return 1;
  }
  return 0;
}

static void parse_content_type(void)
{
  if (str_starts(&content_type, "multipart/")) {
    if (extract_field(content_type.s + 10, "boundary", &boundary))
      str_splices(&boundary, 0, 0, "--");
  }
}

static void parse_headers(void)
{
  striter i;
  
  striter_loop(&i, &headers, LF) {
    unsigned next = i.start + i.len + 1;

    str_catb(&tmpstr, headers.s + i.start, i.len + 1);

    if (next >= headers.len
	|| !isspace(headers.s[next])) {
      parse_header(&tmpstr);
      str_truncate(&tmpstr, 0);
    }
  }
  parse_content_type();
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
  int in_headers;
  
  obuf_write(out, copyheaders.s, copyheaders.len);
  if (boundary.len > 0) {
    while (ibuf_getstr(&inbuf, &tmpstr, LF)) {
      if (str_start(&tmpstr, &boundary)
	  && tmpstr.s[boundary.len] == LF)
	break;
    }
    in_headers = 1;
    while (ibuf_getstr(&inbuf, &tmpstr, LF)) {
      if (str_start(&tmpstr, &boundary)) {
	if (tmpstr.s[boundary.len] == LF) {
	  in_headers = 1;
	  continue;
	}
	else if (memcmp(tmpstr.s + boundary.len, "--\n", 3) == 0)
	  break;
      }
      if (tmpstr.s[0] == LF) {
	in_headers = 0;
	obuf_putc(out, LF);
      }
      else if (!in_headers) {
	if (opt_numlines-- == 0)
	  return;
	obuf_putstr(out, &tmpstr);
      }
    }
  }
  else {
    while (ibuf_getstr(&inbuf, &tmpstr, LF)) {
      if (opt_numlines-- == 0)
	return;
      obuf_putstr(out, &tmpstr);
    }
  }
}

static const char* usage_str =
"usage: %s [-cqDNT] [-H STR] [-h str] [-n NUM] [-s STR] [-t TIME] %s\n"
" -D       Don't remove old response records\n"
" -H STR   List of headers to omit copying into the response\n"
" -N       Don't send, just send autoresponse to standard output\n"
" -c       Copy message into response\n"
" -h STR   List of headers to copy into the response, separated by ':'\n"
" -l NUM   Limit the number of lines of the message that are copied\n"
" -n NUM   Set the maximum number of replies (defaults to 1)\n"
" -s STR   Add the subject to the autoresponse, prefixed by STR\n"
" -t TIME  Set the time interval, in seconds (defaults to 1 hour)\n"
" -q       Don't show error messages\n"
" If more than NUM messages are received from the same sender\n"
" within TIME seconds of each other, no response is sent.\n"
" If both -h and -H are specified, only -h is used.\n"
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
  while((ch = getopt(argc, argv, "DH:Nch:l:n:qs:t:")) != EOF) {
    switch(ch) {
    case 'c': opt_copymsg = 1; break;
    case 'h': opt_headerkeep = optarg; break;
    case 'l':
      opt_numlines = strtoul(optarg, &ptr, 10);
      if(*ptr)
	usage("Invalid number of lines.");
      break;
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
    case 'H': opt_headerstrip = optarg; break;
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
  if(lseek(0, 0, SEEK_SET) == -1)
    fail_temp("Could not rewind input message.");
  read_headers();
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
