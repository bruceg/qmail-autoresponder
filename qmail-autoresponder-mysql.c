#include <mysql/mysql.h>
#include <stdlib.h>
#include <string.h>
#include <str/str.h>
#include "qmail-autoresponder.h"

const char usage_args[] = "USERNAME DOMAIN";
const char usage_post[] =
"  All the options except for -q, -D, and -N can be overridden\n"
"  in the autoresponder SQL table.\n";

static long autoresponder;
static MYSQL mysql;
static str query;

static int str_cats_quoted(str* s, const char* p)
{
  if (!str_catc(s, '\'')) return 0;
  for (; *p != 0; ++p) {
    switch (*p) {
    case '\'':
    case '\\':
      if (!str_catc(s, '\\')) return 0;
    default:
      if (!str_catc(s, *p)) return 0;
    }
  }
  return str_catc(s, '\'');
}

void init_autoresponder(int argc, char** argv)
{
  const char* username;
  const char* domain;
  MYSQL_RES* result;
  MYSQL_ROW row;
  unsigned long* lengths;
  
  if (argc != 2) usage("Incorrect number of command-line arguments.");
  username = argv[0];
  domain = argv[1];

  mysql_init(&mysql);
  mysql_options(&mysql, MYSQL_READ_DEFAULT_GROUP, "qmail-autoresponder");
  if (!mysql_real_connect(&mysql, getenv("MYSQL_HOST"),
			  getenv("MYSQL_USER"), getenv("MYSQL_PASS"),
			  getenv("MYSQL_DB"),
			  0, getenv("MYSQL_SOCKET"), 0))
    fail_temp("Could not connect to MySQL");

  str_copys(&query,
	    "SELECT id,response,"
	    "       opt_timelimit,opt_msglimit,"
	    "       opt_copymsg,opt_subject_prefix "
	    "FROM autoresponder "
	    "WHERE username=");
  str_cats_quoted(&query, username);
  str_cats(&query, " AND domain=");
  str_cats_quoted(&query, domain);
  if (mysql_real_query(&mysql, query.s, query.len))
    fail_temp("Could not select autoresponder.");
  if ((result = mysql_store_result(&mysql)) == 0)
    fail_temp("Error fetching result from database.");
  if (mysql_num_rows(result) == 0)
    exit(0);
  if ((row = mysql_fetch_row(result)) == 0 ||
      (lengths = mysql_fetch_lengths(result)) == 0)
    fail_temp("Error fetching autoresponder from database.");
  else {
    autoresponder = atol(row[0]);
    str_copyb(&response, row[1], lengths[1]);
    if (row[2]) opt_timelimit = atol(row[2]);
    if (row[3]) opt_msglimit = atol(row[3]);
    if (row[4]) opt_copymsg = atol(row[4]) != 0;
    if (row[5]) opt_subject_prefix = strdup(row[5]);
    mysql_free_result(result);
  }
}

int count_history(const char* sender)
{
  unsigned count = opt_msglimit;
  int send_response;
  MYSQL_RES* result;
  MYSQL_ROW row;
  
  if (!opt_nodelete) {
    str_copys(&query, "DELETE FROM response "
	      "WHERE autoresponder=");
    str_cati(&query, autoresponder);
    str_cats(&query, " AND timestamp < (now() - INTERVAL ");
    str_catu(&query, opt_timelimit);
    str_cats(&query, " SECOND)");
    if (mysql_real_query(&mysql, query.s, query.len))
      fail_temp("Could not delete old records from database.");
  }
  
  str_copys(&query, "SELECT count(*) "
	    "FROM response "
	    "WHERE sent_response <> 0 "
	    "AND autoresponder=");
  str_cati(&query, autoresponder);
  str_cats(&query, " AND sender=");
  str_cats_quoted(&query, sender);
  str_cats(&query, " AND timestamp > (now() - INTERVAL ");
  str_catu(&query, opt_timelimit);
  str_cats(&query, " SECOND)");
  if (mysql_real_query(&mysql, query.s, query.len))
    fail_temp("Could not select count of records from database.");
  if ((result = mysql_store_result(&mysql)) == 0 ||
      (row = mysql_fetch_row(result)) == 0)
    fail_temp("Error fetching count of records from database.");
  else {
    count = atol(row[0]);
    mysql_free_result(result);
  }

  send_response = count < opt_msglimit;
  
  str_copys(&query,
	    "INSERT INTO response "
	    "(autoresponder,timestamp,sent_response,sender) "
	    "VALUES (");
  str_cati(&query, autoresponder);
  str_cats(&query, ",now(),");
  str_catu(&query, send_response);
  str_catc(&query, ',');
  str_cats_quoted(&query, sender);
  str_cats(&query, ")");
  if (mysql_real_query(&mysql, query.s, query.len))
    fail_temp("Could not insert response record into database.");

  return send_response;
}
