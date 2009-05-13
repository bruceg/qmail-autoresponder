#include <mysql/mysql.h>
#include <stdlib.h>
#include <string.h>
#include <str/str.h>
#include "qmail-autoresponder.h"

const char usage_args[] = "USERNAME DOMAIN [TABLE_PREFIX]";
const char usage_post[] =
"  All the options except for -q, -D, and -N can be overridden\n"
"  in the autoresponder SQL table.\n";

static long autoresponder;
static MYSQL mysql;
static str query;
static const char* prefix;

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

static MYSQL_RES* do_select(const char* username, const char* domain)
{
  MYSQL_RES* result;
  str_copy3s(&query,
	     "SELECT * "
	     "FROM ", prefix, "autoresponder "
	     "WHERE username");
  if (username) {
    str_catc(&query, '=');
    str_cats_quoted(&query, username);
  }
  else
    str_cats(&query, " IS NULL");
  str_cats(&query, " AND domain=");
  str_cats_quoted(&query, domain);
  if (mysql_real_query(&mysql, query.s, query.len))
    fail_temp("Could not select autoresponder.");
  if ((result = mysql_store_result(&mysql)) == 0)
    fail_temp("Error fetching result from database.");
  return result;
}

static void handle_field(const char* name,
			 const char* value,
			 unsigned int length)
{
  if (strcmp(name, "id") == 0)
    autoresponder = atol(value);
  else if (strcmp(name, "response") == 0)
    str_copyb(&response, value, length);
  else if (memcmp(name, "opt_", 4) == 0)
    handle_option(name + 4, value, length);
}

void init_autoresponder(int argc, char** argv)
{
  const char* username;
  const char* domain;
  MYSQL_RES* result;
  MYSQL_ROW row;
  unsigned long* lengths;
  unsigned int num_fields;
  unsigned int i;
  MYSQL_FIELD* fields;
  
  if (argc < 2 || argc > 3)
    usage("Incorrect number of command-line arguments.");
  username = argv[0];
  domain = argv[1];
  prefix = (argc > 2) ? argv[2] : "";

  mysql_init(&mysql);
  mysql_options(&mysql, MYSQL_READ_DEFAULT_GROUP, "qmail-autoresponder");
  if (!mysql_real_connect(&mysql, getenv("MYSQL_HOST"),
			  getenv("MYSQL_USER"), getenv("MYSQL_PASS"),
			  getenv("MYSQL_DB"),
			  0, getenv("MYSQL_SOCKET"), 0))
    fail_temp("Could not connect to MySQL");

  result = do_select(username, domain);
  if (mysql_num_rows(result) == 0) {
    mysql_free_result(result);
    result = do_select(NULL, domain);
    if (mysql_num_rows(result) == 0)
      exit(0);
  }

  if ((num_fields = mysql_num_fields(result)) == 0
      || (fields = mysql_fetch_fields(result)) == 0
      || (row = mysql_fetch_row(result)) == 0
      || (lengths = mysql_fetch_lengths(result)) == 0)
    fail_temp("Error fetching autoresponder from database.");
  else {
    for (i = 0; i < num_fields; ++i)
      if (row[i])
	handle_field(fields[i].name, row[i], lengths[i]);
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
    str_copy3s(&query, "DELETE FROM ", prefix, "response "
	       "WHERE autoresponder=");
    str_cati(&query, autoresponder);
    str_cats(&query, " AND timestamp < (now() - INTERVAL ");
    str_catu(&query, opt_timelimit);
    str_cats(&query, " SECOND)");
    if (mysql_real_query(&mysql, query.s, query.len))
      fail_temp("Could not delete old records from database.");
  }
  
  str_copy3s(&query, "SELECT count(*) "
	     "FROM ", prefix, "response "
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
  
  str_copy3s(&query,
	     "INSERT INTO ", prefix, "response "
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
