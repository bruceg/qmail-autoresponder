#!/bin/bash

rm -rf testdir
mkdir testdir
cd testdir

cat >message.txt <<EOF
Subject: autoresponse

test
EOF

export DTLINE="Delivered-To: somebody
"

ar() {
  export SENDER="$1"
  shift 1
  echo "Test SENDER=$SENDER; $@"
  {
    echo 'From: somebody'
    for line in "$@"
    do
      echo "$line"
    done
  } | ../autoresponder -N message.txt . >/dev/null
}

set -e

exitfn() {
  echo "The last test failed!"
  exit 1
}
trap exitfn EXIT

ar 'me@my'
! ar ''
! ar '#@[]'
! ar 'mailer-daemon@here'
! ar 'mailer-daemonx@here'
! ar 'mailer-daemon'
! ar 'me@my'
! ar $RANDOM@$RANDOM	'list-id: list'
! ar $RANDOM@$RANDOM	'mailing-list: list'
! ar $RANDOM@$RANDOM	'x-mailing-list: list'
! ar $RANDOM@$RANDOM	'x-ml-name: list'
ar $RANDOM@$RANDOM	'precedence: other'
! ar $RANDOM@$RANDOM	'precedence: junk'
! ar $RANDOM@$RANDOM	'precedence: bulk'
! ar $RANDOM@$RANDOM	'precedence: list'
! ar $RANDOM@$RANDOM	'delivered-to: somebody'

trap - EXIT
