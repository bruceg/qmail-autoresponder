#ifndef QMAIL_AUTORESPONDER__H__
#define QMAIL_AUTORESPONDER__H__

struct obuf;
struct str;

extern int opt_quiet;
extern int opt_copymsg;
extern int opt_nodelete;
extern int opt_nosend;
extern int opt_notoline;
extern int opt_no_inreplyto;
extern unsigned long opt_timelimit;
extern unsigned long opt_msglimit;
extern unsigned long opt_numlines;
extern const char* opt_subject_prefix;
extern const char* opt_headerkeep;
extern const char* opt_headerstrip;
extern const char* opt_separator;
extern const char* opt_bcc;

struct option
{
  const char* name;
  void* ptr;
  void (*copyfn)(void* dest, const char* value, unsigned int length);
};

extern struct option options[];
extern void handle_option(const char* name,
			  const char* value,
			  unsigned int length);

extern time_t now;
extern struct str boundary;
extern struct str copyheaders;
extern struct str message_id;
extern struct str response;
extern struct str subject;
extern struct str tmpstr;

void fail_msg(const char* msg);
void fail_temp(const char* msg);
void usage(const char*);
void ignore(const char* msg);

int qmail_start(void);
void qmail_finish(const char* sender);

void read_parse_headers(void);

void build_response(struct obuf* out);

/* Defined by the individual modules */
extern const char usage_args[];
extern const char usage_post[];
void init_autoresponder(int argc, char** argv);
int count_history(const char* sender);
void log_sender(const char* sender, int responded);

#endif
