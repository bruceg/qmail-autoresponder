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
    (
        sed -e '/^$/,$d' < config.txt
        echo "$1=$2"
        echo
        sed -e '0,/^$/d' < config.txt
    ) > config.new
    mv config.new config.txt
}
unset_option() {
    sed -i -e "/^$1=/d" config.txt
}
set_message() {
    (
        sed -e '/^$/,$d' < config.txt
        echo
        cat
    ) > config.new
    mv config.new config.txt
}
count_log() {
    wc -l < log.txt
}

run_all
