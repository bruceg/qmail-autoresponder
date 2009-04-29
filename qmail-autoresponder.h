#ifndef QMAIL_AUTORESPONDER__H__
#define QMAIL_AUTORESPONDER__H__

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

extern const char* argv0;
extern time_t now;
extern struct str response;

void fail_msg(const char* msg);
void fail_temp(const char* msg);
void usage(const char*);

int qmail_start(void);
void qmail_finish(const char* sender);

/* Defined by the individual modules */
extern const char usage_args[];
extern const char usage_post[];
void init_autoresponder(int argc, char** argv);
int count_history(const char* sender);

#endif
