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
  export SENDER="$1"
  args="$2"
  shift 2
  echo "Test SENDER=$SENDER; $@"
  {
    echo 'From: somebody'
    echo 'Subject: something'
    for line in "$@"
    do
      echo "$line"
    done
  } >tempfile
  ../qmail-autoresponder -N $args message.txt . <tempfile >stdout 2>stderr
}

set -e

exitfn() {
  echo "The last test failed!"
  exit 1
}
trap exitfn EXIT

# Should send response normally
ar 'me@my.domain' ''
# Check that the response contains the sender in a to: header
egrep -q '^To: <me@my.domain>$' stdout
# Don't send immediately to the same recipient
! ar 'me@my.domain' ''
# Should send again after rate limit has expired
sleep 2
ar 'me@my.domain' '-t 1'

# Don't send to empty sender (mail daemon)
! ar '' ''
# Don't send to <#@[]> (qmail mail daemon)
! ar '#@[]' ''
# Don't send to mailer-daemon
! ar 'mailer-daemon@here' ''
# Don't send to mailer-daemon*
! ar 'mailer-daemonx@here' ''
# Don't send to addresses without a hostname
! ar 'somebody' ''

# Don't send to mailing lists
! ar one@my.domain ''	'list-id: list'
! ar two@my.domain ''	'mailing-list: list'
! ar three@my.domain ''	'x-mailing-list: list'
! ar four@my.domain ''	'x-ml-name: list'
ar five@my.domain ''	'precedence: other'
! ar six@my.domain ''	'precedence: junk'
! ar seven@my.domain ''	'precedence: bulk'
! ar eight@my.domain ''	'precedence: list'
! ar nine@my.domain ''	'delivered-to: somebody'

# Check that the subject line can get added to response
ar ten@my.domain '-s Re:' 'subject: subject test'
egrep -q '^Subject: Re: subject test$' stdout

# Check for operation of "-T" option
ar eleven@my.domain '-T'
egrep -vq '^To:' stdout

trap - EXIT
