#!/bin/bash
export MYSQL_DB=qmail-autoresponder

. runtests-common.sh

mysql $MYSQL_DB < ../schema.mysql

mysql $MYSQL_DB <<EOF
INSERT INTO autoresponder VALUES (
  NULL, NULL, 'my.domain',
  'From: nobody in particular\ntest\ntest %S test\n',
  1, 1, NULL, NULL
);
EOF

ar() {
  succeeds=$1
  export SENDER="$2"
  update="$3"
  if [ -n "$update" ]; then
    mysql -e "UPDATE autoresponder SET $update" $MYSQL_DB
  fi
  echo "Test SENDER=$SENDER; $update"

  make_message >tempfile
  runqa $succeeds -mysql me my.domain
}

# Should send response normally
ar true  'me@my.domain' ''
# Check that the response contains the sender in a to: header
egrep -q '^To: <me@my.domain>$' stdout
# Don't send immediately to the same recipient
ar false 'me@my.domain' ''
# Should send again after rate limit has expired
sleep 2
ar true  'me@my.domain' 'opt_timelimit=1'

trap - EXIT
