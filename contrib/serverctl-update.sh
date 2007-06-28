#!/bin/sh

run() {

# The local server, that this is pulling from
# and updating the configuration of.
SERVER="localhost"

cd $1

while ! mkdir serverctl.lock
do
  sleep 1
done


[ -f serverctl.mtn ] || mtn -d serverctl.mtn db init

CLIENTCONF="--db $(pwd)/serverctl.mtn --confdir $(pwd) --rcfile $(pwd)/serverctl-update.lua"


mtn $CLIENTCONF pull $SERVER $(cat serverctl-branch) --quiet || exit $?

if [ -d serverctl ]
then
    (cd serverctl && mtn $CLIENTCONF update)
else
    mtn $CLIENTCONF checkout -b $(cat serverctl-branch) serverctl
fi

mtn $CLIENTCONF pull $SERVER '' --exclude 'ctl-branch-updated' --quiet

rmdir serverctl.lock

}

run $1 &