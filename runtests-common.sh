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
  suffix=$2
  shift 2

  if ! ../qmail-autoresponder$suffix -N "$@" <tempfile >stdout 2>stderr
  then
    echo "qmail-autoresponder$suffix failed"
    exit 1
  fi
  if $succeeds && fgrep -q 'Ignoring message:' stderr; then
    echo "qmail-autoresponder$suffix ignored message"
    exit 1
  fi
  if ! { $succeeds || fgrep -q 'Ignoring message:' stderr; } ; then
    echo "qmail-autoresponder$suffix did not ignore message"
    exit 1
  fi
}

set -e

exitfn() {
  echo "The last test failed!"
  exit 1
}
trap exitfn EXIT

