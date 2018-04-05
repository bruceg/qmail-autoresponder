#!/bin/sh

. ./runtests-common.sh

touch log.txt

runqabin() {
    ../qmail-autoresponder -N "$@" .
}

set_option() {
    echo "$2" > $1
}
unset_option() {
    rm -f $1
}
set_message() {
    cat > message.txt
}
count_log() {
    wc -l < log.txt
}

run_all
