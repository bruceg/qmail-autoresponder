#!/bin/bash

. runtests-common.sh
export MSGFILE="message.txt"

cat >message.txt <<EOF
From: nobody in particular

test
test %S test
EOF

# This big message splits the "%S" across a buffer boundary
cat >big-message.txt <<EOF
From: nobody in particular

sage with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
This is a very long test message with lots of boring contents
Thi%S
EOF

ar() {
  succeeds=$1
  export SENDER="$2"
  args="$3"
  shift 3

  echo "Test SENDER=$SENDER; $@"
  make_message "$@" >tempfile
  runqa $succeeds '' "$MSGFILE" . $args
}

# Should send response normally
ar true  'me@my.domain' ''
# Check that the response contains the sender in a to: header
egrep -q '^To: <me@my.domain>$' stdout
# Don't send immediately to the same recipient
ar false 'me@my.domain' ''
# Should send again after rate limit has expired
sleep 2
ar true  'me@my.domain' '-t 1'

# Don't send to empty sender (mail daemon)
ar false '' ''
# Don't send to <#@[]> (qmail mail daemon)
ar false '#@[]' ''
# Don't send to mailer-daemon
ar false 'mailer-daemon@here' ''
# Don't send to mailer-daemon*
ar false 'mailer-daemonx@here' ''
# Don't send to addresses without a hostname
ar false 'somebody' ''

# Don't send to mailing lists
ar false list-id@my.domain ''	'list-id: list'
ar false mailing-list@my.domain ''	'mailing-list: list'
ar false x-mailing-list@my.domain ''	'x-mailing-list: list'
ar false x-ml-name@my.domain ''	'x-ml-name: list'
ar false list-help@my.domain ''	'list-help: somewhere'
ar false list-unsubscribe@my.domain ''	'list-unsubscribe: somewhere'
ar false list-subscribe@my.domain ''	'list-subscribe: somewhere'
ar false list-post@my.domain ''	'list-post: somewhere'
ar false list-owner@my.domain ''	'list-owner: somewhere'
ar false list-archive@my.domain ''	'list-archive: somewhere'
ar true  other@my.domain ''	'precedence: other'
ar false junk@my.domain ''	'precedence: junk'
ar false bulk@my.domain ''	'precedence: bulk'
ar false list@my.domain ''	'precedence: list'

# Don't send if my DTLINE is in the message
ar false samedt@my.domain ''	'delivered-to: somebody'

# Check that the subject line can get added to response
ar true  ten@my.domain '-s Re:' 'subject: subject test'
egrep -q '^Subject: Re:subject test$' stdout
egrep -q '^test subject test test$' stdout

# Check if the parser can handle a "%S" split across buffers
MSGFILE=big-message.txt
ar true  eleven@my.domain '' 'subject: subject test'
egrep -q '^Thisubject test$' stdout

# Check if source messages are copied into the response properly
MSGFILE=message.txt
ar true nocopy@my.domain ''
! egrep -q '^X-Header: test' stdout

ar true copyall@my.domain '-c'
egrep -q '^X-Header: test' stdout
egrep -q '^plain text$' stdout
! egrep -q 'Content-Type: text/plain' stdout
egrep -q '^<html>HTML</html>$' stdout
! egrep -q 'Should not see this' stdout

ar true mimekeep1@my.domain '-c -h subject:x-header'
egrep -q '^X-Header: test' stdout

ar true mimekeep2@my.domain '-c -h subject'
! egrep -q '^X-Header: test' stdout

ar true mimestrip1@my.domain '-c -H subject:x-h*'
! egrep -q '^X-Header: test' stdout

ar true mimestrip2@my.domain '-c -H subject'
egrep -q '^X-Header: test' stdout

ar true numlines@my.domain '-c -l 1'
egrep -q '^plain text$' stdout
! egrep -q '^<html>HTML</html>$' stdout

trap - EXIT