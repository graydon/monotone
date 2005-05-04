#!/bin/bash
#Timothy Brownawell

#Bash script for profiling.
#This uses oprofile, which I believe needs to be run as root.
#You are assumed to be using a debug version of libc, and to have a
#version of libstdc++ compiled with both normal optimizations and debugging
#symbols left in. (-O2 -g1 seems to be about right)
#This script assumes that the debug libc is used by default.
#This script probably assume a lot more, too.

#The directory holding the files to test with
DATADIR=/mnt/bigdisk

#Database to use with the server for pull
MTDB=mt.db

#A database holding only a keypair.
EMPTYDB=empty.db

#${KPATCH} is the "small patch" to add to the kernel.
#file ${KVER}.tar.bz2 is the kernel tarball to use
KPATCH=patch-2.6.11.7.bz2
KVER=linux-2.6.11

#Full path of the monotone binary to use.
MONOTONE=/mnt/bigdisk/src-managed/monotone-src/monotone

#sudo command, if not set you must be root
#used to run opcontrol
SUDO=/usr/bin/sudo

#Full path of the debug c++ library to use.
#DBG_LIB=/usr/lib/debug/libstdc++.so
DBG_LIB=/usr/local/src/gcc-3.3-3.3.5/gcc-3.3.5/libstdc++-v3/src/.libs/libstdc++.so.5.0.7

#Directory to store the generated profiles in.
PROFDIR=/mnt/bigdisk/monotone-profiles


TIME=/usr/bin/time

VERSION=$(${MONOTONE} --version | sed 's/.*: \(.*\))/\1/')

#Select the most "interesting" looking parts of the generated profiles.
hilights()
{
	F=$(tempfile)
	egrep -v '^(  [[:digit:]]|-)' < $1 | \
	sed 's/([^()]*)/(...)/g' | \
	sed ':x ; s/<\(<\.\.\.>\|[^<>]\)*[^<>\.]>/<...>/g ; t x' > $F
	cat <(head -n1 $1) <(sort -rnk3 $F |head -n 20) <(echo) <(sort -rnk1 $F | head -n 20)
}

run_tests()
{
	TIME1_server=$(tempfile)
	TIME1_client=$(tempfile)
	TIME2=$(tempfile)
	TIME3=$(tempfile)
	TIME4=$(tempfile)
	${SUDO} opcontrol --separate=lib --callgraph=10 --image=${MONOTONE} --no-vmlinux


	echo 'Pull (and serve) net.venge.monotone...'
	cp ${DATADIR}/${EMPTYDB} ${DATADIR}/test.db
	cp ${DATADIR}/${MTDB}    ${DATADIR}/test-serve.db
	${SUDO} opcontrol --reset
	${SUDO} opcontrol --start 
	${TIME} -o ${TIME1_server} ${MONOTONE} --db=${DATADIR}/test-serve.db --ticker=none --quiet serve localhost net.venge.monotone &
	sleep 5 #wait for server to be ready
	${TIME} -o ${TIME1_client} ${MONOTONE} --db=${DATADIR}/test.db --ticker=none --quiet pull localhost net.venge.monotone
	#If we kill the time process, we don't get our statistics.
	kill $(ps -Af|grep 'monotone.*serve\ localhost' | grep -v time | awk '{print $2}')
	opcontrol --dump
	opstack ${MONOTONE} > ${PDIR}/profile-netsync
	${SUDO} opcontrol --shutdown
	hilights ${PDIR}/profile-netsync >${PDIR}/hilights-netsync
	rm ${DATADIR}/test.db ${DATADIR}/test-serve.db


	bzip2 -dc ${DATADIR}/${KVER}.tar.bz2 | tar -C ${DATADIR} -xf -
	pushd ${DATADIR}/${KVER}
	${MONOTONE} setup .
	${MONOTONE} --quiet add $(ls|grep -v '^MT')
	cp ${DATADIR}/${EMPTYDB} ${DATADIR}/test.db

	echo 'Commit the kernel to an empty database...'
	${SUDO} opcontrol --reset
	${SUDO} opcontrol --start
	${TIME} -o ${TIME2} ${MONOTONE} --branch=linux-kernel --db=${DATADIR}/test.db --quiet commit --message="Commit message."
	opcontrol --dump
	opstack ${MONOTONE} > ${PDIR}/profile-commitfirst
	${SUDO} opcontrol --shutdown
	hilights ${PDIR}/profile-commitfirst >${PDIR}/hilights-commitfirst

	echo 'Commit a small patch to the kernel...'
	bzip2 -dc ${DATADIR}/${KPATCH} | patch -p1 >/dev/null
	${SUDO} opcontrol --reset
	${SUDO} opcontrol --start
	${TIME} -o ${TIME3} ${MONOTONE} --branch=linux-kernel --db=${DATADIR}/test.db --quiet commit --message="Commit #2"
	opcontrol --dump
	opstack ${MONOTONE} > ${PDIR}/profile-commit
	${SUDO} opcontrol --shutdown
	hilights ${PDIR}/profile-commit > ${PDIR}/hilights-commit

	echo 'Recommit the kernel without changes...'
	${SUDO} opcontrol --reset
	${SUDO} opcontrol --start
	${TIME} -o ${TIME4} ${MONOTONE} --branch=linux-kernel --db=${DATADIR}/test.db --quiet commit --message="no change"
	opcontrol --dump
	opstack ${MONOTONE} > ${PDIR}/profile-commitsame
	${SUDO} opcontrol --shutdown
	hilights ${PDIR}/profile-commitsame > ${PDIR}/hilights-commitsame

	popd
	rm ${DATADIR}/test.db
	rm -rf ${DATADIR}/${KVER}


	cat <(echo "Serve net.venge.monotone :") ${TIME1_server} >${PDIR}/timing
	cat <(echo -e "\nPull net.venge.monotone :") ${TIME1_client} >>${PDIR}/timing
	cat <(echo -e "\nCommit the kernel (${KVER})to an empty db :") ${TIME2} >>${PDIR}/timing
	cat <(echo -e "\nCommit a small patch (${KPATCH}) to the kernel :") ${TIME3} >>${PDIR}/timing
	cat <(echo -e "\nCommit the kernel without changes :") ${TIME4} >>${PDIR}/timing

	rm ${TIME1_server} ${TIME1_client} ${TIME2} ${TIME3} ${TIME4}
}

BEGINTIME=$(date +%s)

mkdir -p ${PROFDIR}/${VERSION}$1
export LD_PRELOAD=${DBG_LIB}
PDIR=${PROFDIR}/${VERSION}$1
echo "Profiling..."
run_tests

chmod -R a+rX ${PROFDIR}/${VERSION}$1

echo "Monotone version: ${VERSION}"
cat <(echo -e "Timing for each run:\n") ${PROFDIR}/${VERSION}$1/timing
TOTALTIME=$(($(date +%s)-${BEGINTIME}))
echo "Time elapsed (all runs): $((${TOTALTIME}/60)):$((${TOTALTIME}%60))"
