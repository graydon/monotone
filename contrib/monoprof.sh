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
	${SUDO} opcontrol --reset
	${SUDO} opcontrol --start
}

profend()
{
	opcontrol --dump
	opstack ${MONOTONE} > ${PDIR}/profile-${SHORTNAME}
	${SUDO} opcontrol --shutdown
	hilights ${PDIR}/profile-${SHORTNAME} > ${PDIR}/hilights-${SHORTNAME}
	echo -e "\n${TESTNAME}:" >>${PDIR}/timing
	cat ${RUNTIME} >>${PDIR}/timing
	rm ${RUNTIME}
}

#Individual tests to run.
#Each test should clean up after itself.
#Since which tests to run can now be specified on the command line,
#all tests should be independent.

TESTS="${TESTS} test_netsync"
test_netsync()
{
	local TIME_SERVER=$(tempfile);
	local TIME_CLIENT=$(tempfile);
	local SHORTNAME="netsync"
	local TESTNAME="Pull (and serve) net.venge.monotone"
	cp ${DATADIR}/${EMPTYDB} ${DATADIR}/test.db
	cp ${DATADIR}/${MTDB}    ${DATADIR}/test-serve.db
	profstart 
	${TIME} -o ${TIME_SERVER} ${MONOTONE} --db=${DATADIR}/test-serve.db \
		${HOOKS} --ticker=none --quiet serve localhost \
		net.venge.monotone &
	sleep 5 #wait for server to be ready
	${TIME} -o ${TIME_CLIENT} ${MONOTONE} --db=${DATADIR}/test.db \
		${HOOKS} pull localhost net.venge.monotone
	#If we kill the time process, we don't get our statistics.
	kill $(ps -Af|grep 'monotone.*serve\ localhost' | \
		grep -v time | awk '{print $2}')
	#profend
	opcontrol --dump
	opstack ${MONOTONE} > ${PDIR}/profile-${SHORTNAME}
	${SUDO} opcontrol --shutdown
	hilights ${PDIR}/profile-${SHORTNAME} >${PDIR}/hilights-${SHORTNAME}
	echo -e "\nServe net.venge.monotone :" >>${PDIR}/timing
	cat ${TIME_SERVER} >>${PDIR}/timing
	echo -e "\nPull net.venge.monotone :" >>${PDIR}/timing
	cat ${TIME_CLIENT} >>${PDIR}/timing
	rm ${TIME_SERVER} ${TIME_CLIENT}
	
	rm ${DATADIR}/test.db ${DATADIR}/test-serve.db
}

TESTS="${TESTS} test_commit"
test_commit()
{
	local RUNTIME=$(tempfile)
	local TESTNAME="Commit kernel ${KVER} to an empty database"
	local SHORTNAME="commitfirst"
	bzip2 -dc ${DATADIR}/linux-${KVER}.tar.bz2 | tar -C ${DATADIR} -xf -
	pushd ${DATADIR}/${KVER}
	${MONOTONE} ${HOOKS} setup .
	${MONOTONE} ${HOOKS} --quiet add $(ls|grep -v '^MT')
	cp ${DATADIR}/${EMPTYDB} ${DATADIR}/test.db

	profstart
	${TIME} -o ${RUNTIME} ${MONOTONE} ${HOOKS} --branch=linux-kernel \
		--db=${DATADIR}/test.db commit \
		--message="Commit message."
	profend
	
	RUNTIME=$(tempfile)
	TESTNAME="Commit a small patch (${KPATCHVER}) to the kernel"
	SHORTNAME="commitpatch"
	
	bzip2 -dc ${DATADIR}/patch-${KPATCHVER}.bz2 | patch -p1 >/dev/null
	profstart
	${TIME} -o ${RUNTIME} ${MONOTONE} ${HOOKS} --branch=linux-kernel \
		--db=${DATADIR}/test.db commit --message="Commit #2"
	profend
	
	RUNTIME=$(tempfile)
	TESTNAME="Recommit the kernel without changes"
	SHORTNAME="commitsame"
	
	profstart
	${TIME} -o ${RUNTIME} ${MONOTONE} ${HOOKS} --branch=linux-kernel \
		--db=${DATADIR}/test.db commit --message="no change"
	profend

	popd
	rm ${DATADIR}/test.db
	rm -rf ${DATADIR}/linux-${KVER}/
}

TESTS="${TESTS} test_lcad"
test_lcad()
{
	local RUNTIME=$(tempfile)
	local TESTNAME="Find lcad of ebf14142 and 68fe12e6"
	local SHORTNAME="lcad"

	cp ${DATADIR}/${MTDB} ${DATADIR}/test.db
	profstart
	${TIME} -o ${RUNTIME} ${MONOTONE} ${HOOKS} --db=${DATADIR}/test.db \
		lcad ebf14142331667146d7a3aabb406945648ea00de \
		     68fe12e6f1de7d161eb9e27dd757e7d230049520
	
	profend
	rm ${DATADIR}/test.db
}

#Based on tests/t_netsync_largish_file.at
TESTS="${TESTS} test_bigfile"
test_bigfile()
{
	local TIME_SERVER=$(tempfile);
	local TIME_CLIENT=$(tempfile);
	local RUNTIME=$(tempfile)
	local TESTNAME=""#"Netsync a big file."
	local SHORTNAME=""#"bigfile"
#setup:
	pushd ${DATADIR}
	cp ${EMPTYDB} test.db
	cp ${EMPTYDB} test2.db
	${MONOTONE} ${HOOKS} --db=test.db setup testdir
	pushd testdir
	awk -- 'BEGIN{srand(5253);for(a=0;a<32*1024*1024;a+=20)printf("%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c",rand()*256,rand()*256,rand()*256,rand()*256,rand()*256,rand()*256,rand()*256,rand()*256,rand()*256,rand()*256,rand()*256,rand()*256,rand()*256,rand()*256,rand()*256,rand()*256,rand()*256,rand()*256,rand()*256,rand()*256);}' > largish
	${MONOTONE} add largish
	
	TESTNAME="Commit a big file"
	SHORTNAME="bigfile-commit"
	profstart
	${TIME} -o ${RUNTIME} ${MONOTONE} ${HOOKS} commit --branch=bigfile \
		--message="log message" --db=${DATADIR}/test.db
	profend
	
	TESTNAME="Netsync a big file"
	SHORTNAME="bigfile-sync"
	profstart
#run:	
	${TIME} -o ${TIME_SERVER} ${MONOTONE} ${HOOKS} --db=${DATADIR}/test.db \
		--ticker=none --quiet serve localhost bigfile &
	sleep 5 #wait for server to be ready
	${TIME} -o ${TIME_CLIENT} ${MONOTONE} --db=${DATADIR}/test2.db \
		${HOOKS} pull localhost bigfile
	#If we kill the time process, we don't get our statistics.
	kill $(ps -Af|grep 'monotone.*serve\ localhost' | \
		grep -v time | awk '{print $2}')

	#profend
	opcontrol --dump
	opstack ${MONOTONE} > ${PDIR}/profile-${SHORTNAME}
	${SUDO} opcontrol --shutdown
	hilights ${PDIR}/profile-${SHORTNAME} >${PDIR}/hilights-${SHORTNAME}
	echo -e "\nServe an uncompressible 32MB file :" >>${PDIR}/timing
	cat ${TIME_SERVER} >>${PDIR}/timing
	echo -e "\nPull an uncompressible 32MB file :" >>${PDIR}/timing
	cat ${TIME_CLIENT} >>${PDIR}/timing
	
	rm ${TIME_SERVER} ${TIME_CLIENT}
#cleanup:
	popd
	rm -rf testdir/
	rm test.db test2.db
	popd
}

#TESTS="${TESTS} test_name"
#test_name()
#{
#	local RUNTIME=$(tempfile)
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
	${SUDO} opcontrol --separate=lib --callgraph=10 \
		--image=${MONOTONE} --no-vmlinux
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

pushd ${BUILDDIR}
if [ ${PULL} = "true" ] ; then
	monotone pull
fi
if [ ${UPDATE} = "true" ] ; then
	monotone update
fi
if [ ${BUILD} = "true" ] ; then
	make || ( echo -e "Build failed.\nNot profiling." >&2 ; exit 1 )
fi
popd
if ! [ "${RESTRICT}" = "" ] ; then
	TESTS="${RESTRICT}"
fi

if ! [ -f ${MONOTONE} ] ; then
	echo "Error: ${MONOTONE} does not exist." >&2
	exit 1
fi

run_tests
TOTALTIME=$(($(date +%s)-${BEGINTIME}))
ELAPSED=$((${TOTALTIME}/60)):$((${TOTALTIME}%60))
echo -e "\nTime elapsed: ${ELAPSED}\n"
