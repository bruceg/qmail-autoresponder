#!/bin/sh

. ./runtests-common.sh

touch log.txt
touch responses.txt
touch config.txt

runqabin() {
    ../qmail-autoresponder -N "$@" .
}

set_option() {
    unset_option $1
    echo "$1=$2" >> config.txt
}
unset_option() {
    sed -i -e "/^$1=/d" config.txt
}
set_message() {
    cat > message.txt
}
count_log() {
    wc -l < log.txt
}

run_all
