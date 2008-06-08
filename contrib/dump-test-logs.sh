#! /bin/sh

# Dump the detailed logs for all tests with an unexpected outcome to
# stdout.  This is intended for use in super-automated build
# environments, e.g. the Debian build daemons, where the easiest way
# to recover detailed test logs for a failed build is to embed them in
# the overall 'make' output.  Run, with no arguments, from the top
# level of a monotone build tree.

# Conveniently enough, testlib.lua does most of the work for us.

dumped=0
for log in tester_dir/*.log
do
    if grep "^\*\**$" < $log > /dev/null 2>&1; then
	dumped=1
	echo
	echo "### $log ###"
	echo
	sed -ne '/^Running tests/,/^$/!p' < $log
    fi
done

# Exit unsuccessfully if we dumped anything, so that a driver Makefile
# can do something like
#
#        make check || sh dump-test-logs.sh
#
# and have that fail the build just like plain "make check" would.
exit $dumped
