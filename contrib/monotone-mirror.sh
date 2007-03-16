#! /bin/sh
#
# Reads a simple specification with lines describing what to do.
# The lines may be several of the following:
#
#	mirror		SERVER	[OPTIONS ...] PATTERN ...
#
#	postaction	COMMAND
#
# All "mirror" lines describe what to pull from what servers.  Everything
# ends up in one database.
#
# All "postaction" lines describe a command to be evaluated after mirroring
# is done.  When each command is evaluated, the environment variable DATABASE
# contains the name of the database for them to use.
#
# All "mirror" lines are executed first, then all "postaction" lines.
#
# $1	database to use.  Must be initialised beforehand or this will fail!
#	Default: /var/lib/monotone/mirror/mirror.mtn
# $2	specification file name.
#	Default: /etc/monotone/mirror.rc

set -e

database=$1
if [ -z "$database" ]; then
    database=/var/lib/monotone/mirror/mirror.mtn
fi

if [ -f "$database" ]; then :; else
    echo "The database $database doesn't exist" >&2
    echo "You have to initialise it yourself, like this:" >&2
    echo "	mtn db init -d $database" >&2
    exit 1
fi

rc=$2
if [ -z "$rc" ]; then
    rc=/etc/monotone/mirror.rc
fi

if [ -f "$rc" ]; then :; else
    echo "The specification file $rc doesn't exist" >&2
    exit 1
fi

# Make sure the path to the database is absolute
databasedir=`dirname $database`
databasefile=`basename $database`
databasedir=`cd $databasedir; pwd`
database="$databasedir/$databasefile"

mkdir -p $database.redo
mkdir $database.lock1 || \
    (echo 'Database locked by another process'; exit 1) && \
    (
    while [ -d $database.redo ]; do
	rmdir $database.redo
	sed -e '/^#/d' < "$rc" | while read KEYWORD SERVER PATTERNS; do
	    if [ "$KEYWORD" = "mirror" ]; then
		if [ -z "$SERVER" -o -z "$PATTERNS" ]; then
		    echo "Server or pattern missing in line: $SERVER $PATTERNS" >&2
		    echo "Skipping..." >&2
		else
		    ( eval "set -x; mtn -d '$database' --ticker=dot pull $SERVER $PATTERNS" )
		fi
	    fi
	done

	sed -e '/^#/d' < "$rc" | while read KEYWORD COMMAND; do
	    if [ "$KEYWORD" = "postaction" ]; then
		if [ -z "$COMMAND" ]; then
		    echo "Command missing in line: $COMMAND" >&2
		    echo "Skipping..." >&2
		else
		    ( DATABASE="$database" eval "set -x; $COMMAND" )
		fi
	    fi
	done
    done

    rmdir $database.lock1
    )
