#!/bin/bash

rm -rf testdir
mkdir testdir
cd testdir

cat >message.txt <<EOF
From: nobody in particular

test
EOF

export DTLINE="Delivered-To: somebody
"

ar() {
  succeeds=$1
  export SENDER="$2"
  args="$3"
  shift 3
  echo "Test SENDER=$SENDER; $@"
  {
    echo 'From: somebody'
    echo 'Subject: something'
    for line in "$@"
    do
      echo "$line"
    done
  } >tempfile
  if ! ../qmail-autoresponder -N $args message.txt . <tempfile >stdout 2>stderr
  then
    echo "qmail-autoresponder failed"
    exit 1
  fi
  if $succeeds && fgrep -q 'Ignoring message:' stderr; then
    echo "qmail-autoresponder ignored message"
    exit 1
  fi
  if ! { $succeeds || fgrep -q 'Ignoring message:' stderr } ; then
    echo "qmail-autoresponder did not ignore message"
    exit 1
  fi
}

set -e

exitfn() {
  echo "The last test failed!"
  exit 1
}
trap exitfn EXIT

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
ar false one@my.domain ''	'list-id: list'
ar false two@my.domain ''	'mailing-list: list'
ar false three@my.domain ''	'x-mailing-list: list'
ar false four@my.domain ''	'x-ml-name: list'
ar true  five@my.domain ''	'precedence: other'
ar false six@my.domain ''	'precedence: junk'
ar false seven@my.domain ''	'precedence: bulk'
ar false eight@my.domain ''	'precedence: list'

# Don't send if my DTLINE is in the message
ar false nine@my.domain ''	'delivered-to: somebody'

# Check that the subject line can get added to response
ar true  ten@my.domain '-s Re:' 'subject: subject test'
egrep -q '^Subject: Re: subject test$' stdout

# Check for operation of "-T" option
ar true  eleven@my.domain '-T'
egrep -vq '^To:' stdout

trap - EXIT
