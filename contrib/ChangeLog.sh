#!/bin/sh
# Usage: ChangeLog [<num>] [-d database] [-r revision]
# Construct and print a ChangeLog for the last <num> revisions from their
# date, author, and changelog certs.

# If <num> is not given, it defaults to 15

# If this script is not run from the root of a monotone workspace, both
# the -d and -r options are required.

NUM=15
MTN=mtn

while ! [ $# -eq 0 ] ; do
	case "$1" in
		-r) shift; REV="$1";;
		-d) shift; DB="-d $1";;
		*)  NUM=$(($1 - 1));;
	esac
	shift
done

if [ "x$REV" = "x" ]; then
	REV=`mtn automate get_base_revision_id 2>/dev/null`
fi

if [ "x$REV" = "x" ] || [ "x$DB" = "x" ]; then
	if ! mtn status >/dev/null 2>/dev/null; then
		echo "Both the -d and -r arguments are needed when this" >&2;
		echo "script is not run from inside a monotone workspace." >&2;
		exit 1;
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
	$MTN $DB ls certs "$2" | sed "$1" \
	| sed 's/^[^\:]\+\: //g'
}

getrevs()
{
	$MTN $DB automate ancestors "$1" \
	| $MTN $DB automate toposort -@- \
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
	echo "_MTN/revision does not exist!" >&2
fi
