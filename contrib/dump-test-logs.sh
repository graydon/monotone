#! /bin/sh

# Dump the detailed logs for all tests with an unexpected outcome to
# stdout.  This is intended for use in super-automated build
# environments, e.g. the Debian build daemons, where the easiest way
# to recover detailed test logs for a failed build is to embed them in
# the overall 'make' output.  Run, with no arguments, from the top
# level of a monotone build tree.

set -e
cd tester_dir

dumped=0
for log in */*/tester.log
do
    label=${log%/tester.log}
    status=${log%/tester.log}/STATUS

    if [ -f "$status" ]; then
	shorttag=$(cat "$status")
	case "$shorttag" in
	    ok | skipped* | expected\ failure* | partial\ skip )
		continue ;;
	esac
    else
	shorttag="no status file"
    fi

    if [ $dumped -eq 0 ]; then
	echo "### Detailed test logs:"
	dumped=1
    fi
    echo "### $label	$shorttag"
    cat "$log"
done

# Exit unsuccessfully if we dumped anything, so that a driver Makefile
# can do something like
#
#        make check || sh dump-test-logs.sh
#
# and have that fail the build just like plain "make check" would.
exit $dumped
