#!/bin/sh
set -e
if [ "$1" = "" ]; then
    MONOTONE=`pwd`/monotone
elif [ -x "$1" ]; then
    MONOTONE="$1"
else
    echo "Usage: $0 [monotone-binary-to-test [test-to-run ...]]"
    exit 1
fi

if [ ! -x $MONOTONE ]; then
    echo "$MONOTONE doesn't exist?!"
    exit 1
fi

PARSE_ACCOUNT=`pwd`/contrib/parse-accounting.pl
if [ -x $PARSE_ACCOUNT ]; then
    :
elif [ -x `dirname $MONOTONE`/contrib/parse-accounting.pl ]; then
    PARSE_ACCOUNT=`dirname $MONOTONE`/contrib/parse-accounting.pl
else
    echo "can't find parse-accounting.pl.  Looked in `pwd`/contrib, and `dirname $MONOTONE`/contrib"
    exit 1
fi

MONOTONE_DB=`cat MT/options | grep database | awk '{print $2}' | sed 's/^.//' | sed 's/.$//'`
if [ -z "$MONOTONE_DB" -o ! -f "$MONOTONE_DB" ]; then
    echo "Couldn't auto-determine monotone db?!"
    exit 1
fi
[ -d /tmp/mt-perf-test ] || mkdir /tmp/mt-perf-test
cd /tmp/mt-perf-test

# figure out if binary has timing built in...
ENABLE_MONOTONE_STATISTICS=1 $MONOTONE --help >timing-check.out 2>&1
if [ `grep '^STATS: ' timing-check.out | wc -l` -gt 1 ]; then
    MEASURE=
    PIDFILE_ARG=
    KILLBY=child
    echo "Using builtin statistics..."
else
    MEASURE=time
    PIDFILE_ARG=--pid-file=/tmp/mt-perf-test/pid-file
    KILLBY=file
    echo "Using external statistics..."
fi

[ -d staging ] || mkdir staging
cd staging

# Rebuild all of the various files for testing ...

if [ ! -f random.large ]; then
    echo "rebuilding random.large (this takes a long time)..."
    dd if=/dev/urandom of=random.large-new bs=1024k count=100 >/dev/null 2>&1
    mv random.large-new random.large
fi

for i in 0 1 2; do 
    for j in 0 1 2 3 4 5 6 7 8 9; do 
	if [ ! -f random.medium.$i$j ]; then
	    echo "rebuilding random.medium.$i$j..."
	    dd if=/dev/urandom of=random.medium-new bs=1024k count=10 >/dev/null 2>&1
	    mv random.medium-new random.medium.$i$j
	fi
    done
done
    
if [ ! -f halfzero.large ]; then
    echo "rebuilding halfzero.large..."
    dd if=/dev/zero of=halfzero.large-new bs=1024k count=50 >/dev/null 2>&1
    dd if=/dev/urandom of=halfzero.large-new bs=1024k seek=50 count=50 >/dev/null 2>&1
    mv halfzero.large-new halfzero.large
fi

if [ ! -d monotone ]; then
    [ ! -d monotone-new ] || rm -rf monotone-new
    mkdir monotone-new
    # revisions 0.10 .. 0.22
    for i in 713ed1966baced883ed865a931f97259522f90da fdc32bcc09e2714350fb514990bd26acb607264b 3cd6b8cc947ddab015fd945d3c305fc748bb6d0a 95a1a16c0941cc1ae51e9eb5d64d075ef35c5b19 fdf1335b4dfd8c1529fef8db58e5b819b03f7c8a 20b36b747dcce1230a6e7a0b1554bd7874f0fbe7 35da5df64546301d332303bbf63b6799d70932c8 e8c9e4eb0534a4c6e538935576330de34ec42052 168adf9537ff136c9b7fe7faad5991f92859390d 44ed8807bead656889fb5022f974e13a7169098c e65bc11b6670a0b2ed8e72214cb81d94e6a9a2d1 28058ae3e850229a5d8fae65415cbbf82b435377; do
	echo "checking out monotone rev $i..."
	$MONOTONE --db $MONOTONE_DB checkout --revision $i monotone-new/mt-$i
    done
    # Version 0.17, had to specify the branch explicitly to get checkout to work.
    i=337d62e5cbd50c36e2f2c2bda489a98de3a8aeb7 
    echo "checking out monotone rev $i..."
    $MONOTONE --db $MONOTONE_DB checkout --branch net.venge.monotone --revision $i monotone-new/mt-$i
    mv monotone-new/mt-168adf9537ff136c9b7fe7faad5991f92859390d monotone-new/mt-0.19
    rm -rf monotone-new/*/MT
    mv monotone-new monotone
fi

cd /tmp/mt-perf-test
if [ ! -d dbdir ]; then
    [ ! -d dbdir-new ] || rm -rf dbdir-new
    mkdir dbdir-new
    cd dbdir-new
    cat >monotonerc <<EOF
function get_passphrase(keypair_id)
  return "c2fba42ffaac67b09575c6db9139d5f6"
end

function get_netsync_read_permitted (collection, identity)
  return true
end

function get_netsync_write_permitted (collection, identity)
  return true
end
EOF
    cat >keys <<EOF
[pubkey perf-test-id]
MIGdMA0GCSqGSIb3DQEBAQUAA4GLADCBhwKBgQCjEdB1Vr/Y8yYBKoeDXUzsyzEPJATFn4ve
5LSD3I5qIDBffGDk39lAjsPyv28HFtNmQPqRcZSIHu4d3BYvlnLRaaIdqQuArl/NqcNXVmtY
sY45ezC4MOeAP0PvbvmL97xgkDFjY5IjQ2fMSj6BPx7XXbJ/O5TGSxavZUWPKAs1HwIBEQ==
[end]

[privkey perf-test-id]
xReoEfvdTW8awd4kNC4OUxuN9PlI3prLaupef+2lT5ve77vxqdKoe0EZkT+zXBCxEOykWaez
7GTFHbTSWNr5N8r7RJ81kHkXVVoMIMuv6DK1lMNXXttsvpXBZR/M7H0UtJnylN/2PIqLkBlF
TkXTKc0bd1enSbobbZBIsWFKENpUYQZsb7EmslPIVZiqWAXGAzHuIN49+WrrCssm1jzb5fEk
IBF7k7tMnQ6EFlSzT9CdLDuafdDX9Lrpb4jB8qPL1VVx5uP/ENHqVgOvqKHRBSk0DiTfY8fs
5n19cSQYDUDkc5N3AF7GRXD38tJEixpnSkMoRpiucL5NZ59vjgsO567B8V07Trr5D0ZXoUXz
m2KZWyVrP/Qgq3aog+hRuJfNLfqDiVvCtm3ypaXV1mik7NBdKfe01TogzK3jx/XIiCxyH7Qn
vq2UXLYjJ1QAOQrYY6YNtcouZvSO04B5H7V92uT19bs/yp7fZBk2LmqIMGs+Gbk0Yk9RS1gC
N+YHZKsnO9YkqmlLFx8rCIvctGWo+iJhoTjl2aYIQ6nyL0HC3iM+kskzmzXj8HkJaL9edSqj
iipd/ct8dR6058ntOJ9QvWnewS65lW1DjIfqPGDUyUWYHhJHNPe8auqredbKbLw7+FiahD46
kUeKnALMLo5LUHDbFMWI+ezFk0Yr02WRhNFHTIP8ETrxlXlXWYnFL0h1yiuQtf0925N5pBh0
5Ez+/JS6mkAxxoph9zqvrciXdub7LnBHTzW1SKv7lgxYiqF1fjd+pyP7ayJH59ZvckOPArpq
vwTMSutpiF65z7aI+9hfSSAip4ySR1QQ/jFychhwwK/1B6aDR4lK
[end]
EOF
    cd ..
    mv dbdir-new dbdir
fi

load_zero_small() {
    dd if=/dev/zero of=zero.small bs=1k count=10 >/dev/null 2>&1
}

load_zero_large() {
    dd if=/dev/zero of=zero.large bs=1024k count=100 >/dev/null 2>&1
}

load_random_medium() {
    cp -rp ../staging/random.medium.00 .
}

load_random_medium_20() {
    cp -rp ../staging/random.medium.[01]? .
}

load_halfzero_large() {
    cp -rp ../staging/halfzero.large .
}

load_random_large() {
    cp -rp ../staging/random.large .
}

load_monotone() {
    cp -rp ../staging/monotone/mt-0.19 .
}

load_mt_multiple() {
    cp -rp ../staging/monotone .
}

load_mt_bigfiles() {
    for i in ../staging/monotone/mt-*; do
	j=`basename $i`
	find $i -type f -exec cat '{}' \; >$j.txt
    done
}

load_mixed() {
    if [ "$1" = "" ]; then
	echo "Usage load_mixed #"
	exit 1
    fi
    RANDOM_LIST=(`ls ../staging/random.medium.??`)
    MONOTONE_LIST=(`ls -d ../staging/monotone/mt-*`)
    dd if=/dev/zero of=zero.med bs=1024k count=10 >/dev/null 2>&1
    i=0;
    j=0;
    while [ $i -lt $1 ]; do
	MT_PATH=${MONOTONE_LIST[$i]}
	MT_NAME=`basename $MT_PATH`
	[ -d $MT_NAME ] || cp -rp $MT_PATH .
	cp ${RANDOM_LIST[$j]} $MT_NAME
	cat zero.med >>$MT_NAME/`basename ${RANDOM_LIST[$j]}`
	j=`expr $j + 1`
	cp ${RANDOM_LIST[$j]} $MT_NAME
	cat zero.med >>$MT_NAME/`basename ${RANDOM_LIST[$j]}`
	j=`expr $j + 1`
	i=`expr $i + 1`
    done
}

load_mixed_1() {
    load_mixed 1
}

load_mixed_4() {
    load_mixed 4
}

load_mixed_12() {
    load_mixed 12
}

load_everything() {
    # print something out here even though it means removing
    # from the output because it takes so long.
    echo -n "load everything...";
    load_zero_small
    echo -n "."
    load_zero_large
    echo -n "."
    load_random_medium_20
    echo -n "."
    load_halfzero_large
    echo -n "."
    load_random_large
    echo -n "."
    load_mt_multiple
    echo -n "."
    load_mt_bigfiles
    echo -n "."
    load_mixed_12
    echo "."
}

prep_test() {
    cd /tmp/mt-perf-test/dbdir
    [ ! -f checkin.db ] || rm checkin.db
    [ ! -f netsync.db ] || rm netsync.db
    [ ! -f checkin.db-journal ] || rm checkin.db-journal
    [ ! -f netsync.db-journal ] || rm netsync.db-journal

    $MONOTONE --db checkin.db db init >/tmp/mt-perf-test/log 2>&1
    $MONOTONE --db netsync.db db init >>/tmp/mt-perf-test/log 2>&1
    cat keys | $MONOTONE --db checkin.db read >>/tmp/mt-perf-test/log 2>&1
    cat keys | $MONOTONE --db netsync.db read >>/tmp/mt-perf-test/log 2>&1
    
    cd /tmp/mt-perf-test
    [ ! -d testdir ] || rm -rf testdir
    $MONOTONE --db dbdir/checkin.db setup --branch test testdir >>/tmp/mt-perf-test/log 2>&1
    cd testdir
}

dotest() {
    prep_test
    load_$1
    export ENABLE_MONOTONE_STATISTICS=1
    $MEASURE $MONOTONE add . >/tmp/mt-perf-test/add.log 2>&1
    $PARSE_ACCOUNT "$1" "add files" /tmp/mt-perf-test/add.log

    cp ../dbdir/monotonerc MT/monotonerc
    $MEASURE $MONOTONE commit -m foo >/tmp/mt-perf-test/commit.log 2>&1
    $PARSE_ACCOUNT "$1" "commit" /tmp/mt-perf-test/commit.log

    cd ..
    rm -rf testdir
    $MEASURE $MONOTONE --db dbdir/checkin.db checkout --branch test testdir >/tmp/mt-perf-test/checkout.log 2>&1
    $PARSE_ACCOUNT "$1" "checkout" /tmp/mt-perf-test/checkout.log
    
    cd testdir
    cp ../dbdir/monotonerc MT/monotonerc
    [ -f /tmp/mt-perf-test/pid-file ] && rm /tmp/mt-perf-test/pid-file
    $MEASURE $MONOTONE --db ../dbdir/checkin.db $PIDFILE_ARG serve localhost:7318 test >/tmp/mt-perf-test/serve.log 2>&1 &
    SERVER=$!
    sleep 1
    $MEASURE $MONOTONE --db ../dbdir/netsync.db pull localhost:7318 test >/tmp/mt-perf-test/pull.log 2>&1
    # SEGV here is intentional, it causes the server to exit through an 
    # assertion which prints out the accounting information
    case $KILLBY in
	 child) kill -SEGV $SERVER ;;
	 file) kill -SEGV `cat /tmp/mt-perf-test/pid-file` ;;
	 *) echo "internal error, unknown killby '$KILLBY'" ;;
    esac
    wait $SERVER || true
    $PARSE_ACCOUNT "$1" "serve" /tmp/mt-perf-test/serve.log
    $PARSE_ACCOUNT "$1" "pull" /tmp/mt-perf-test/pull.log
    echo
    unset ENABLE_MONOTONE_STATISTICS
}

echo -n "Test CPU: "
grep 'model name' /proc/cpuinfo | sed 's/model name.: //' | head -1
$MONOTONE --version 2>&1 | grep 'base revision'
ENABLE_MONOTONE_STATISTICS=1 $MEASURE $MONOTONE --help >/tmp/mt-perf-test/help.log 2>&1
$PARSE_ACCOUNT header header /tmp/mt-perf-test/help.log
if [ "$2" != "" ]; then
    shift
    while [ "$1" != "" ]; do
	dotest "$1"
	shift
    done
    exit 0
fi
dotest zero_small
dotest zero_large
dotest random_medium
dotest random_medium_20
dotest halfzero_large
dotest random_large
dotest monotone
dotest mt_multiple
dotest mt_bigfiles
dotest mixed_1
dotest mixed_4
dotest mixed_12
dotest everything

