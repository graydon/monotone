#!/bin/sh
# Usage: ChangeLog [<num>] [-d database] [-r revision]
# Construct and print a ChangeLog for the last <num> revisions from their
# date, author, and changelog certs.

# If <num> is not given, it defaults to 15

# If this script is not run from the root of a monotone workspace, both
# the -d and -r options are required.

NUM=15

while ! [ $# -eq 0 ] ; do
	case "$1" in
		-r) shift; REV="$1";;
		-d) shift; DB="-d $1";;
		*)  NUM=$(($1 - 1));;
	esac
	shift
done

if ! [ -s MT/revision ]; then
	if [ "x$REV" = "x" ] || [ "x$DB" = "x" ]; then
		echo "Both the -d and -r arguments are needed when this" >&2;
		echo "script is not run from the root of a monotone workspace." >&2;
		exit 1;
	fi
else
	if [ "x$REV" = "x" ]; then
		REV=`cat MT/revision`
	fi
fi

# Get the contents of a cert
LOG='/^Name.*changelog$/,/^----/! D; /^Name/ D; /^----/ D'
DATE='/^Name.*date$/,/^----/! D; /^Name/ D; /^----/ D'
AUTHOR='/^Name.*author$/,/^----/! D; /^Name/ D; /^----/ D'

# Remove "duplicate" lines (When the date+author line is unneeded because
# the same info is in the changelog cert (won't match exactly, but should
# both start with ^${year} ))
# Keep the line from the changelog, instead of the generated one
RD=':b; N; /^[[:digit:]]\{4\}.*\n[[:digit:]]\{4\}/ { s/^.*\n//; b b; }; P; D'

get()
{
	monotone $DB ls certs "$2" | sed "$1" \
	| sed 's/^[^\:]\+\: //g'
}

getrevs()
{
	monotone $DB automate ancestors "$1" \
	| monotone $DB automate toposort -@- \
	| tail -n "$2" | tac
}

getlogs()
{
	for i in "$REV" `getrevs "$REV" "$NUM"`; do
		echo `get "$DATE" "$i"` '' `get "$AUTHOR" "$i"`
		get "$LOG" "$i" | sed 's/^\([^[:digit:]\t]\)/\t\1/g'
	done
}


if [ ! x$REV = x ]; then
	getlogs | sed "$RD" | sed '/^$/ d' \
	| sed 's/^\([[:digit:]]\{4\}.*\)$/\n\1\n/g'
else
	echo "MT/revision does not exist!" >&2
fi
