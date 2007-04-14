#ifndef QMAIL_AUTORESPONDER__H__
#define QMAIL_AUTORESPONDER__H__

#include <str/str.h>

extern int opt_quiet;
extern int opt_copymsg;
extern int opt_nodelete;
extern int opt_nosend;
extern int opt_notoline;
extern int opt_no_inreplyto;
extern time_t opt_timelimit;
extern unsigned opt_msglimit;
extern const char* opt_subject_prefix;
extern const char* opt_hdrkeep;
extern const char* opt_hdrstrip;

extern const char* argv0;
extern time_t now;
extern str response;

void fail_msg(const char* msg);
void fail_temp(const char* msg);
void usage(const char*);

/* Defined by the individual modules */
extern const char usage_args[];
extern const char usage_post[];
void init_autoresponder(int argc, char** argv);
int count_history(const char* sender);

#endif
