rm -rf testdir
mkdir testdir
cd testdir

DTLINE="Delivered-To: somebody
"

export DTLINE

not() {
  if "$@"; then
    false;
  fi
}

make_message() {
  echo 'From: somebody'
  echo 'Subject: something'
  for line in "$@"
  do
    echo "$line"
  done
  echo 'Content-Type: multipart/alternative; boundary="boundary"'
  echo 'Message-Id: <message.id.123@my.domain>'
  echo 'X-Header: test'
  echo
  echo '--boundary'
  echo 'Content-Type: text/plain'
  echo
  echo 'plain text'
  echo '--boundary'
  echo 'Content-Type: text/html'
  echo
  echo '<html>HTML</html>'
  echo '--boundary--'
  echo 'Content-Type: text/plain'
  echo
  echo 'Should not see this'
}

runqa() {
  succeeds=$1
  shift 1

  if ! runqabin -N "$@" <tempfile >stdout 2>stderr
  then
    echo "qmail-autoresponder failed"
    exit 1
  fi
  if $succeeds && fgrep -q 'Ignoring message:' stderr; then
    echo "qmail-autoresponder ignored message"
    exit 1
  fi
  if ! { $succeeds || fgrep -q 'Ignoring message:' stderr; } ; then
    echo "qmail-autoresponder did not ignore message"
    exit 1
  fi
}

runtest() {
  succeeds=$1
  export SENDER="$2"
  args="$3"
  shift 3

  echo "Test SENDER=$SENDER; $@"
  make_message "$@" >tempfile
  runqa $succeeds $args
}

set -e

exitfn() {
  echo "The last test failed!"
  exit 1
}
trap exitfn EXIT

run_all() {
    set_message <<EOF
From: nobody in particular

test
test %S test
EOF
    # Should send response normally
    runtest true  'me@my.domain' ''

    # Check that the response contains the sender in a to: header
    sed -e '/^$/,$d' < stdout | grep -q '^To: <me@my.domain>$'
    sed -e '/^$/,$d' < stdout | grep -q '^In-Reply-To: <message.id.123@my.domain>$'
    # Check that the response contains the text in the body
    sed -e '1,/^$/d' < stdout | grep -q '^test$'
    sed -e '1,/^$/d' < stdout | grep -q '^test something test$'
    # Don't send immediately to the same recipient
    runtest false 'me@my.domain' ''

    # Should send again after rate limit has expired
    sleep 2
    runtest true 'me@my.domain' '-t 1'
    sleep 2
    runtest true 'me@my.domain' '-O timelimit=1'
    sleep 2
    set_option timelimit 1
    runtest true 'me@my.domain' ''

    # Check behavior of no_inreplyto
    runtest true 'noinreplyto@my.domain' '-O no_inreplyto'
    not egrep -q '^In-Reply-To: <message.id.123@my.domain>$' stdout

    set_option no_inreplyto 1
    runtest true 'noinreplyto-opt@my.domain' ''
    not egrep -q '^In-Reply-To: <message.id.123@my.domain>$' stdout

    # Don't send to empty sender (mail daemon)
    runtest false '' ''
    # Don't send to <#@[]> (qmail mail daemon)
    runtest false '#@[]' ''
    # Don't send to mailer-daemon
    runtest false 'mailer-daemon@here' ''
    # Don't send to mailer-daemon*
    runtest false 'mailer-daemonx@here' ''
    # Don't send to addresses without a hostname
    runtest false 'somebody' ''

    # Don't send to mailing lists
    runtest false list-id@my.domain ''	'list-id: list'
    runtest false mailing-list@my.domain ''	'mailing-list: list'
    runtest false x-mailing-list@my.domain ''	'x-mailing-list: list'
    runtest false x-ml-name@my.domain ''	'x-ml-name: list'
    runtest false list-help@my.domain ''	'list-help: somewhere'
    runtest false list-unsubscribe@my.domain ''	'list-unsubscribe: somewhere'
    runtest false list-subscribe@my.domain ''	'list-subscribe: somewhere'
    runtest false list-post@my.domain ''	'list-post: somewhere'
    runtest false list-owner@my.domain ''	'list-owner: somewhere'
    runtest false list-archive@my.domain ''	'list-archive: somewhere'
    runtest true  other@my.domain ''	'precedence: other'
    runtest false junk@my.domain ''	'precedence: junk'
    runtest false bulk@my.domain ''	'precedence: bulk'
    runtest false list@my.domain ''	'precedence: list'

    # Don't send if my DTLINE is in the message
    runtest false samedt@my.domain ''	'delivered-to: somebody'

    # Check that the subject line can get added to response
    runtest true copysubject@my.domain '-s Re:' 'subject: subject test'
    egrep -q '^Subject: Re:subject test$' stdout
    egrep -q '^test subject test test$' stdout

    runtest true subject_prefix-arg@my.domain '-O subject_prefix=Re:' 'subject: subject test'
    egrep -q '^Subject: Re:subject test$' stdout

    set_option subject_prefix Re:
    runtest true subject_prefix-opt@my.domain '' 'subject: subject test'
    egrep -q '^Subject: Re:subject test$' stdout

    # Check if source messages are copied into the response properly
    runtest true nocopy@my.domain ''
    not egrep -q '^X-Header: test' stdout

    runtest true copyall@my.domain '-c'
    egrep -q '^X-Header: test' stdout
    egrep -q '^plain text$' stdout
    not egrep -q 'Content-Type: text/plain' stdout
    egrep -q '^<html>HTML</html>$' stdout
    not egrep -q 'Should not see this' stdout

    set_option copymsg 0
    runtest true copymsg0@my.domain ''
    not egrep -q '^X-Header: test' stdout

    set_option copymsg 1
    runtest true copymsg1@my.domain ''
    egrep -q '^X-Header: test' stdout
    egrep -q '^plain text$' stdout
    egrep -q '^<html>HTML</html>$' stdout

    runtest true headerkeep2@my.domain '-O headerkeep=subject:x-header'
    egrep -q '^X-Header: test' stdout

    set_option headerkeep 'subject:x-header'
    runtest true headerkeep2-opt@my.domain ''
    egrep -q '^X-Header: test' stdout
    unset_option headerkeep

    runtest true headerkeep1@my.domain '-O headerkeep=subject'
    not egrep -q '^X-Header: test' stdout

    set_option headerkeep 'subject'
    runtest true headerkeep1-opt@my.domain ''
    not egrep -q '^X-Header: test' stdout

    unset_option headerkeep
    runtest true headerstrip2@my.domain '-O headerstrip=subject:x-h*'
    not egrep -q '^X-Header: test' stdout

    set_option headerstrip 'subject:x-h*'
    runtest true headerstrip2-opt@my.domain ''
    not egrep -q '^X-Header: test' stdout

    unset_option headerstrip
    runtest true headerstrip1@my.domain '-O headerstrip=subject'
    egrep -q '^X-Header: test' stdout

    set_option headerstrip 'subject'
    runtest true headerstrip1-opt@my.domain ''
    egrep -q '^X-Header: test' stdout

    runtest true numlines@my.domain '-O numlines=1'
    egrep -q '^plain text$' stdout
    not egrep -q '^<html>HTML</html>$' stdout

    set_option numlines 1
    runtest true numlines-opt@my.domain ''
    egrep -q '^plain text$' stdout
    not egrep -q '^<html>HTML</html>$' stdout

    # This big message splits the "%S" across a buffer boundary
    set_message <<EOF
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

    # Check if the parser can handle a "%S" split across buffers
    runtest true big-msg-subject@my.domain '' 'subject: subject test'
    egrep -q '^Thisubject test$' stdout

    log_count=$( count_log )
    if [ $log_count != 26 ]
    then
        echo "Wrong number of log entries: $log_count"
        exit 1
    else
        echo "Correct number of log entries."
    fi

    trap - EXIT
    echo All tests passed.
}
