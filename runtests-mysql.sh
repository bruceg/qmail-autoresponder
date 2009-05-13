#!/bin/bash
export MYSQL_DB=qmail-autoresponder

. runtests-common.sh

prefix=test_
sed -e "s/@PREFIX@/$prefix/g" ../schema.mysql.in \
| mysql $MYSQL_DB

mysql $MYSQL_DB <<EOF
INSERT INTO ${prefix}autoresponder (username,domain,opt_timelimit,opt_msglimit,response)
VALUES (
  NULL, 'my.domain', 1, 1,
  'From: nobody in particular\ntest\ntest %S test\n'
);
EOF

update() {
  mysql -e "UPDATE ${prefix}autoresponder SET $*" $MYSQL_DB
}

ar() {
  succeeds=$1
  export SENDER="$2"
  update="$3"
  if [ -n "$update" ]; then
    update "$update"
  fi
  echo "Test SENDER=$SENDER; $update"

  make_message >tempfile
  runqa $succeeds -mysql -- me my.domain $prefix
}

# Should send response normally
ar true  'me@my.domain' ''
# Check that the response contains the sender in a to: header
egrep -q '^To: <me@my.domain>$' stdout
egrep -q '^In-Reply-To: <message.id.123@my.domain>$' stdout
# Don't send immediately to the same recipient
ar false 'me@my.domain' ''
# Should send again after rate limit has expired
sleep 2
ar true  'me@my.domain' 'opt_timelimit=1'

# Check to make sure option fields are honored
ar true 'noinreplyto@my.domain' 'opt_no_inreplyto=1'
not egrep -q '^In-Reply-To: <message.id.123@my.domain>$' stdout

ar true copymsg0@my.domain 'opt_copymsg=0'
not egrep -q '^X-Header: test' stdout

ar true copymsg1@my.domain 'opt_copymsg=1'
egrep -q '^X-Header: test' stdout
egrep -q '^plain text$' stdout
egrep -q '^<html>HTML</html>$' stdout

ar true headerkeep2@my.domain "opt_headerkeep='subject:x-header'"
egrep -q '^X-Header: test' stdout

ar true headerkeep1@my.domain "opt_headerkeep='subject'"
not egrep -q '^X-Header: test' stdout

update "opt_headerkeep=NULL"

ar true headerstrip2@my.domain "opt_headerstrip='subject:x-h*'"
not egrep -q '^X-Header: test' stdout

ar true headerstrip1@my.domain "opt_headerstrip='subject:'"
egrep -q '^X-Header: test' stdout

ar true numlines@my.domain "opt_numlines=1"
egrep -q '^plain text$' stdout
not egrep -q '^<html>HTML</html>$' stdout

if [ $( mysql --table -e "SELECT * FROM ${prefix}log" $MYSQL_DB \
        | wc -l ) != 15 ]; then
  echo Wrong number of records in the log table.
  exit 1
else
  echo Log table has the right number of records.
fi

trap - EXIT
echo All tests passed.
