#!/bin/bash
#Timothy Brownawell

print_help()
{
cat <<EOF
Arguments: [flags ...] [testname ...]
    --build       Rebuild monotone before profiling.
    --update      Run "monotone update" before building. Implies --build
    --pull        Run "monotone pull" before update. Implies --update, --build
    --append x    Append ".x" to the profile directory name
    --list        List available profile tests
    --overwrite   Allow results to be placed in an already existing directory
    --datadir x   Use x as base directory for files used, scratchwork, results
    --help        Print this message
    --setup       Set up most of the needed files (broken?)
    --what x      x is one of (time, mem); what to profile (defaults to time)
    testname      Only run selected profile tests
EOF
}

#Bash script for profiling.
#The user running this script must have sudo permission for opcontrol.
#You are assumed to be using a debug version of libc, and to have a
#version of libstdc++ compiled with both normal optimizations and debugging
#symbols left in. (-O2 -g1 seems to be about right)
#This script assumes that the debug libc is used by default.
#This script probably assume a lot more, too.
#
#Files (any of these can be symlinks):
#	${DATADIR}/mt.db		db holding net.venge.monotone
#	${DATADIR}/empty.db		db holding a keypair
#	${DATADIR}/linux-2.6.11.tar.bz2
#	${DATADIR}/patch-2.6.11.7.bz2
#	${DATADIR}/monotone-src/	dir with checked-out n.v.m
#	${DATADIR}/libstdc++.so		optimized debug stdc++ library
#	${DATADIR}/monotone-profiles/	directory to store profiles in
#						created if nonexistent
#	${DATADIR}/hooks.lua		optional, hooks to use instead of
#					the default

#The directory holding the files to test with
#Can be overridden from the command line.
DATADIR=/mnt/bigdisk

#Some other variables depend on $DATADIR, which can be set on the command line.
#So, the option handling needs to be done here or those vars might be wrong.
BUILD=false
UPDATE=false
PULL=false
HELP=false
LIST=false
APPEND=""
RESTRICT=""
OVERWRITE=false
SETUP=false
WHAT=""
while ! [ $# -eq 0 ] ; do
	case "$1" in
		--build) BUILD=true;;
		--update) UPDATE=true; BUILD=true;;
		--pull) PULL=true; UPDATE=true; BUILD=true;;
		--append) shift; APPEND=".$1";;
		--list) LIST="true";;
		--overwrite) OVERWRITE="true";;
		--datadir) shift; DATADIR=$1;;
		--setup) SETUP=true;;
		--what) shift; WHAT=$1;;
		--help) HELP="true";;
		*) RESTRICT="${RESTRICT} $1";;
	esac
	shift
done

#Database holding net.venge.monotone
MTDB=mt.db

#A database holding only a keypair.
EMPTYDB=empty.db

#patch-${KPATCHVER}.bz2 is the "small patch" to add to the kernel.
#file linux-${KVER}.tar.bz2 is the kernel tarball to use
KPATCHVER=2.6.11.7
KVER=2.6.11

#Directory containing monotone sources
BUILDDIR=${DATADIR}/monotone-src
#Full path of the monotone binary to use.
MONOTONE=${BUILDDIR}/monotone

#sudo command, if not set you must be root
#used to run opcontrol
SUDO=/usr/bin/sudo

#command line for valgrind
VALGRIND="valgrind --tool=massif --depth=7"

#Full path of the debug c++ library to use.
#You probably have to build this yourself, since your distro's packaged
#debug library probably isn't optimized (and so will give bogus profiles).
#DBG_LIB=/usr/lib/debug/libstdc++.so
DBG_LIB=${DATADIR}/libstdc++.so

#Directory to store the generated profiles in.
PROFDIR=${DATADIR}/monotone-profiles

#Note: don't just use "time"; bash has a builtin by that name.
#The real thing is better.
TIME=/usr/bin/time

VERSION=""
[ -f ${MONOTONE} ] && VERSION=$(${MONOTONE} --version | sed 's/.*: \(.*\))/\1/')

HOOKS=""
[ -f ${DATADIR}/hooks.lua ] && HOOKS="--norc --rcfile=${DATADIR}/hooks.lua"

server()
{
	TIME_SERVER=$(tempfile)
	if [ "${WHAT}" = "mem" ] ; then
		${TIME} -o ${TIME_SERVER} \
		${VALGRIND} --log-file=srv-log \
		${MONOTONE} ${HOOKS} "$@" &
	else
		${TIME} -o ${TIME_SERVER} \
		${MONOTONE} ${HOOKS} "$@" &
	fi
	sleep 5
}

client()
{
	TIME_CLIENT=$(tempfile)
	if [ "${WHAT}" = "mem" ] ; then
		${TIME} -o ${TIME_CLIENT} \
		${VALGRIND} --log-file=cli-log \
		${MONOTONE} ${HOOKS} "$@"
	else
		${TIME} -o ${TIME_CLIENT} \
		${MONOTONE} ${HOOKS} "$@"
	fi
}

mtn()
{
	RUNTIME=$(tempfile)
	if [ "${WHAT}" = "mem" ] ; then
		${TIME} -o ${RUNTIME} \
		${VALGRIND} --log-file=mtn-log \
		${MONOTONE} ${HOOKS} "$@"
	else
		${TIME} -o ${RUNTIME} \
		${MONOTONE} ${HOOKS} "$@"
	fi
}

mtn_noprof()
{
	${MONOTONE} ${HOOKS} "$@"
}

getsrvpid()
{
	ps -Af|grep 'monotone.*serve\ localhost' | \
		grep -v time | awk '{print $2}'
}

killsrv()
{
	kill -HUP $(getsrvpid)
	while ! [ "$(getsrvpid)" = "" ] ; do
		sleep 1
	done
}


#This picks the top 20 functions for execution time in the function,
#and the top 20 for execution time in children of the function.
#Function and template arguments are replaced with "...".
hilights()
{
	local F=$(tempfile)
	egrep -v '^(  [[:digit:]]|-)' < $1 | \
	sed 's/([^()]*)/(...)/g' | \
	sed ':x ; s/<\(<\.\.\.>\|[^<>]\)*[^<>\.]>/<...>/g ; t x' > $F
	cat <(head -n1 $1) <(sort -rnk3 $F |head -n 20) <(echo) \
		<(sort -rnk1 $F | head -n 20)
	rm $F
}

profstart()
{
	echo "${TESTNAME}..."
	if [ "${WHAT}" = "time" ] ; then
		${SUDO} opcontrol --reset
		${SUDO} opcontrol --start
	fi
}

profend()
{
	if [ "${WHAT}" = "time" ] ; then
		opcontrol --dump
		opstack ${MONOTONE} > ${PDIR}/profile-${SHORTNAME}
		${SUDO} opcontrol --shutdown
		hilights ${PDIR}/profile-${SHORTNAME} > \
			${PDIR}/hilights-${SHORTNAME}
	fi
	
	local PID=""
	
	#Record standalone instance
	if [ -f "${RUNTIME}" ] ; then
		echo -e "\n${TESTNAME}:" >>${PDIR}/timing
		cat ${RUNTIME} >>${PDIR}/timing
		rm ${RUNTIME}
		if [ "${WHAT}" = "mem" ] ; then
			PID=$(echo mtn-log.pid*|sed 's/[^[:digit:]]//g')
			rm mtn-log.pid*
			mv massif.${PID}.txt ${PDIR}/memprof-${SHORTNAME}.txt
			mv massif.${PID}.ps ${PDIR}/memprof-${SHORTNAME}.ps
		fi
	fi
	
	#Record server instance
	if [ -f "${TIME_SERVER}" ] ; then
		echo -e "\n${SERVER_NAME}:" >>${PDIR}/timing
		cat ${TIME_SERVER} >>${PDIR}/timing
		rm ${TIME_SERVER}
		if [ "${WHAT}" = "mem" ] ; then
			PID=$(echo srv-log.pid*|sed 's/[^[:digit:]]//g')
			rm srv-log.pid*
			mv massif.${PID}.txt ${PDIR}/memprof-${SRVNAME}.txt
			mv massif.${PID}.ps ${PDIR}/memprof-${SRVNAME}.ps
		fi
	fi
	
	#Record client instance
	if [ -f "${TIME_CLIENT}" ] ; then
		echo -e "\n${CLIENT_NAME}:" >>${PDIR}/timing
		cat ${TIME_CLIENT} >>${PDIR}/timing
		rm ${TIME_CLIENT}
		if [ "${WHAT}" = "mem" ] ; then
			PID=$(echo cli-log.pid*|sed 's/[^[:digit:]]//g')
			rm cli-log.pid*
			mv massif.${PID}.txt ${PDIR}/memprof-${CLINAME}.txt
			mv massif.${PID}.ps ${PDIR}/memprof-${CLINAME}.ps
		fi
	fi
}

#Individual tests to run.
#Each test should clean up after itself.
#Since which tests to run can now be specified on the command line,
#all tests should be independent.

TESTS="${TESTS} test_netsync"
test_netsync()
{
	local SHORTNAME="netsync"
	local TESTNAME="Pull (and serve) net.venge.monotone"
	local SERVER_NAME="Serve net.venge.monotone"
	local SRVNAME="serve"
	local CLIENT_NAME="Pull net.venge.monotone"
	local CLINAME="pull"
	cp ${DATADIR}/${EMPTYDB} ${DATADIR}/test.db
	cp ${DATADIR}/${MTDB}    ${DATADIR}/test-serve.db
	profstart 
	server --db=${DATADIR}/test-serve.db \
		--ticker=none --quiet serve localhost \
		net.venge.monotone
	client --db=${DATADIR}/test.db \
		pull localhost net.venge.monotone
	killsrv
	profend
	
	rm ${DATADIR}/test.db ${DATADIR}/test-serve.db
}

TESTS="${TESTS} test_commit"
test_commit()
{
	local TESTNAME="Commit kernel ${KVER} to an empty database"
	local SHORTNAME="commitfirst"
	bzip2 -dc ${DATADIR}/linux-${KVER}.tar.bz2 | tar -C ${DATADIR} -xf -
	pushd ${DATADIR}/${KVER}
	mtn_noprof setup .
	mtn_noprof --quiet add . # $(ls|grep -v '^MT')
	cp ${DATADIR}/${EMPTYDB} ${DATADIR}/test.db

	profstart
	mtn --branch=linux-kernel --db=${DATADIR}/test.db commit \
		--message="Commit message."
	profend
	
	TESTNAME="Commit a small patch (${KPATCHVER}) to the kernel"
	SHORTNAME="commitpatch"
	
	bzip2 -dc ${DATADIR}/patch-${KPATCHVER}.bz2 | patch -p1 >/dev/null
	profstart
	mtn --branch=linux-kernel --db=${DATADIR}/test.db commit \
		--message="Commit #2"
	profend
	
	TESTNAME="Recommit the kernel without changes"
	SHORTNAME="commitsame"
	
	profstart
	mtn --branch=linux-kernel --db=${DATADIR}/test.db commit \
		--message="no change"
	profend

	popd
	rm ${DATADIR}/test.db
	rm -rf ${DATADIR}/linux-${KVER}/
}

TESTS="${TESTS} test_lcad"
test_lcad()
{
	local TESTNAME="Find lcad of ebf14142 and 68fe12e6"
	local SHORTNAME="lcad"

	cp ${DATADIR}/${MTDB} ${DATADIR}/test.db
	profstart
	mtn --db=${DATADIR}/test.db \
		lcad ebf14142331667146d7a3aabb406945648ea00de \
		     68fe12e6f1de7d161eb9e27dd757e7d230049520
	
	profend
	rm ${DATADIR}/test.db
}

TESTS="${TESTS} test_bigfile"
test_bigfile()
{
	local TESTNAME=""#"Netsync a big file."
	local SHORTNAME=""#"bigfile"
#setup:
	pushd ${DATADIR}
	cp ${EMPTYDB} test.db
	cp ${EMPTYDB} test2.db
	mtn_noprof --db=test.db setup testdir
	pushd testdir
	dd if=/dev/urandom of=largish bs=1M count=32
	mtn_noprof add largish
	
	TESTNAME="Commit a big file"
	SHORTNAME="bigfile-commit"
	profstart
	mtn commit --branch=bigfile --db=${DATADIR}/test.db \
		--message="log message"
	profend
	
	TESTNAME="Netsync a big file"
	SHORTNAME="bigfile-sync"
	local SERVER_NAME="Serve a big file"
	local SRVNAME="bigfile-serve"
	local CLIENT_NAME="Pull a big file"
	local CLINAME="bigfile-pull"
	profstart
#run:	
	server --db=${DATADIR}/test.db \
		--ticker=none --quiet serve localhost bigfile
	client --db=${DATADIR}/test2.db pull localhost bigfile
	killsrv

	profend
#cleanup:
	popd
	rm -rf testdir/
	rm test.db test2.db
	popd
}

#TESTS="${TESTS} test_name"
#test_name()
#{
#	local TESTNAME=""
#	local SHORTNAME=""
##setup:
#
#	profstart
##run:	
#
#	profend
##cleanup:
#
#}

run_tests()
{
	local PDIR=${PROFDIR}/${VERSION}${APPEND}
	if [ -d ${PDIR} ] && [ ${OVERWRITE} = "false" ] ; then
		echo "Already profiled this version." >&2
		echo "If you've made changes since then use --append" >&2
		echo "to place the new results in a new directory," >&2
		echo "or specify --overwrite." >&2
		exit 1
	fi
	mkdir -p ${PDIR}
	export LD_PRELOAD=${DBG_LIB}
	echo "Profiling..."
	if [ "${WHAT}" = "time" ] ; then
		${SUDO} opcontrol --separate=lib --callgraph=10 \
			--image=${MONOTONE} --no-vmlinux
	fi
	for i in ${TESTS}; do
		$i
	done
	chmod -R a+rX ${PDIR}
	echo "Monotone version: ${VERSION}"
	cat <(echo -e "Timing for each run:") ${PROFDIR}/${VERSION}$1/timing
}

BEGINTIME=$(date +%s)

if [ ${HELP} = "true" ] ; then
	print_help
	exit 0
fi
if [ ${LIST} = "true" ] ; then
	for i in ${TESTS}; do
		echo -e "\t$i"
	done
	exit 0
fi
if [ ${SETUP} = "true" ] ; then
	pushd ${DATADIR}
	monotone --db=empty.db db init
	echo -e "xxx\nxxx\n" | monotone --db=empty.db genkey xxx
	echo "function get_passphrase(keypair_id)" >hooks.lua
	echo "return \"xxx\"" >>hooks.lua
	echo "end" >>hooks.lua
	cp empty.db mt.db
	monotone --db=mt.db pull off.net net.venge.monotone
	monotone --db=mt.db --branch=net.venge.monotone co monotone-src
	wget http://www.kernel.org/pub/linux/kernel/v2.6/patch-2.6.11.7.bz2
	wget http://www.kernel.org/pub/linux/kernel/v2.6/linux-2.6.11.tar.bz2
	if ! [ -f ${DEBUG_LIB} ] ; then
		echo "You still need to build a debug stdlibc++"
		echo "and copy or symlink it to ${DEBUG_LIB}"
	fi
	popd
	exit 0
fi

pushd ${BUILDDIR} >/dev/null
if [ ${PULL} = "true" ] ; then
	monotone pull
fi
if [ ${UPDATE} = "true" ] ; then
	monotone update
fi
if [ ${BUILD} = "true" ] ; then
	make || ( echo -e "Build failed.\nNot profiling." >&2 ; exit 1 )
fi
popd >/dev/null
if ! [ "${RESTRICT}" = "" ] ; then
	TESTS="${RESTRICT}"
fi

if ! [ -f ${MONOTONE} ] ; then
	echo "Error: ${MONOTONE} does not exist." >&2
	exit 1
fi

TIME_OK=false
if which opcontrol >/dev/null && which opstack >/dev/null; then
	TIME_OK=true
fi

MEM_OK=false
if which valgrind >/dev/null; then
	MEM_OK=true
fi

if [ "${WHAT}" = "time" ] ; then
	if ! [ ${TIME_OK} = "true" ] ; then
		echo "Error: cannot find oprofile." >&2
		exit 1
	fi
fi

if [ "${WHAT}" = "mem" ] ; then
	if ! [ ${MEM_OK} = "true" ] ; then
		echo "Error: cannot find valgrind." >&2
		exit 1
	fi
fi

if [ "${WHAT}" = "" ] ; then
	if [ ${MEM_OK} = "true" ] ; then
		WHAT="mem"
	fi
	if [ ${TIME_OK} = "true" ] ; then
		WHAT="time"
	fi
fi

if [ "${WHAT}" = "" ] ; then
	echo "Error: cannot find a profiler." >&2
	exit 1
fi

run_tests
TOTALTIME=$(($(date +%s)-${BEGINTIME}))
ELAPSED=$((${TOTALTIME}/60)):$((${TOTALTIME}%60))
echo -e "\nTime elapsed: ${ELAPSED}\n"
