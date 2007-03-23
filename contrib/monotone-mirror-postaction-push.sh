#! /bin/sh
#
# Reads a simple specification in the following form:
#
#	SERVER		PATTERN ...
#
# For each line, the given branch PATTERNs are pushed to the given
# SERVER. SERVER has the following syntax: ADDRESS[:PORT]
#
# This script relies on the following environment variables:
#
#	DATABASE	points out what database to use as source.
#	KEYDIROPT	has the form '--keydir=<KEYDIRECTORY>' it the top
#			mirror script has a keydir setting.
#	KEYIDOPT	has the form '--key=<KEYID>' it the top mirror
#			script has a keyid setting.
#
# $1	specification file name.
#	Default: /etc/monotone/push.rc

if [ -z "$DATABASE" ]; then
    echo "No database was given through the DATABASE environment variable" >&2
    exit 1
fi

if [ -f "$DATABASE" ]; then :; else
    echo "The database $DATABASE doesn't exist" >&2
    echo "You have to initialise it yourself, like this:" >&2
    echo "	mtn db init -d $database" >&2
    exit 1
fi

rc=$1
if [ -z "$rc" ]; then
    rc=/etc/monotone/push.rc
fi

if [ -f "$rc" ]; then :; else
    echo "The specification file $rc doesn't exist" >&2
    exit 1
fi

# Make sure the path to the database is absolute
databasedir=`dirname $DATABASE`
databasefile=`basename $DATABASE`
databasedir=`cd $databasedir; pwd`
database="$databasedir/$databasefile"

sed -e '/^#/d' < "$rc" | while read SERVER PATTERNS; do
    if [ -n "$SERVER" -a -n "$PATTERNS" ]; then
	( eval "set -x; mtn -d \"$database\" $KEYDIROPT $KEYIDOPT push $SERVER $PATTERNS" )
    elif [ -n "$SERVER" -o -n "$PATTERNS" ]; then
	echo "SYNTAX ERROR IN LINE '$SERVER $PATTERNS'"
    fi
done
