#!/bin/sh
export MYSQL_DB=qmail-autoresponder

. ./runtests-common.sh

prefix=test_
sed -e "s/@PREFIX@/$prefix/g" ../schema.mysql.in \
| mysql $MYSQL_DB

mysql $MYSQL_DB <<EOF
INSERT INTO ${prefix}autoresponder (username,domain,opt_timelimit,opt_msglimit,response)
VALUES (
  NULL, 'my.domain', 1, 1,
  'From: nobody in particular\n\ntest\ntest %S test\n'
);
EOF

runqabin() {
    ../qmail-autoresponder-mysql "$@" -- me my.domain $prefix
}

update() {
    mysql -e "UPDATE ${prefix}autoresponder SET $*" $MYSQL_DB
}
set_option() {
    update "opt_$1='$2'"
}
unset_option() {
    update "opt_$1=NULL"
}
set_message() {
    msg=$( cat )
    update "response='$msg'"
}
count_log() {
    mysql --vertical --skip-column-names \
          --execute="SELECT COUNT(*) FROM ${prefix}log" $MYSQL_DB \
          | tail -n 1
}

run_all
