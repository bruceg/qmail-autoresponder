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

static void write_response(obuf* out, const char* sender, const char* recipient)
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
      case 'r':
        obuf_puts(out, recipient);
        ++buf; --len;
        break;
      case 's':
        obuf_puts(out, sender);
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
  
  if (opt_separator) {
    obuf_write(out, "\n\n", 2);
    obuf_puts(out, opt_separator);
  }
  obuf_write(out, "\n\n", 2);
  obuf_putstr(out, &copyheaders);
  obuf_write(out, "\n", 1);
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

void build_response(obuf* out, const char* sender, const char* recipient)
{
  write_response(out, sender, recipient);
  if (opt_copymsg)
    copy_input(out);
}
