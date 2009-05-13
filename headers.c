#include <sysdeps.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <iobuf/iobuf.h>
#include <msg/msg.h>
#include <path/path.h>
#include <str/str.h>
#include <str/iter.h>
#include <systime.h>
#include "qmail-autoresponder.h"

str boundary = {0,0,0};
str copyheaders = {0,0,0};
str message_id = {0,0,0};
str subject = {0,0,0};

static const char* dtline;
static size_t dtline_len;
static str content_type;
static str headers;

static void ignore_ml(const str* s, const char* header)
{
  unsigned hdrlen = strlen(header);
  if (strncasecmp(s->s, header, hdrlen) == 0 && s->s[hdrlen] == ':') {
    fail_msg("Ignoring message:");
    msgf("{Message appears to be from a mailing list (}s{ header)}", header);
    exit(0);
  }
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

void read_parse_headers(void)
{
  dtline = getenv("DTLINE");
  if(!dtline)
    usage("DTLINE is not set; must be run from qmail.");
  dtline_len = strlen(dtline);

  // Read and parse header
  if(lseek(0, 0, SEEK_SET) == -1)
    fail_temp("Could not rewind input message.");
  read_headers();
  parse_headers();
}
