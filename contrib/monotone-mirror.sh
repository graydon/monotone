#! /bin/sh
#
# Reads a simple specification with lines describing what to do.
# The lines may be several of the following:
#
#	key		KEYDIR KEYID
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
# "key" lines are always executed in place, and affect any subsequent "mirror"
# or "postaction" line.
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
    touch $database.keyvars
    while [ -d $database.redo ]; do
	rmdir $database.redo
	sed -e '/^#/d' < "$rc" | while read KEYWORD ARGS; do
	    case $KEYWORD in
		key)
		    set $ARGS
		    keydir=$1
		    keyid=$2
		    keydiropt=""
		    keyidopt=""
		    if [ -n "$keydir" ]; then keydiropt="--keydir='$keydir'"; fi
		    if [ -n "$keyid" ]; then keyidopt="--key='$keyid'"; fi
		    (
			echo "keydiropt=\"$keydiropt\""
			echo "keyidopt=\"$keyidopt\""
		    ) > $database.keyvars
		    ;;
		mirror)
		    echo "$ARGS" | while read SERVER PATTERNS; do
			if [ -z "$SERVER" -o -z "$PATTERNS" ]; then
			    echo "Server or pattern missing in line: $SERVER $PATTERNS" >&2
			    echo "Skipping..." >&2
			else
			    ( 
				. $database.keyvars
				eval "set -x; mtn -d '$database' '$keydiropt' '$keyidopt' --ticker=dot pull $SERVER $PATTERNS"
			    )
			fi
		    done
	    esac
	done

	sed -e '/^#/d' < "$rc" | while read KEYWORD ARGS; do
	    case $KEYWORD in
		key)
		    set $ARGS
		    keydir=$1
		    keyid=$2
		    keydiropt=""
		    keyidopt=""
		    if [ -n "$keydir" ]; then keydiropt="--keydir='$keydir'"; fi
		    if [ -n "$keyid" ]; then keyidopt="--key='$keyid'"; fi
		    (
			echo "keydiropt=\"$keydiropt\""
			echo "keyidopt=\"$keyidopt\""
		    ) > $database.keyvars
		    ;;
		postaction)
		    if [ -z "$ARGS" ]; then
			echo "Command missing in line: $ARGS" >&2
			echo "Skipping..." >&2
		    else
			(
			    . $database.keyvars
			    DATABASE="$database" \
				KEYDIROPT="$keydiropt" KEYIDOPT="$keyidopt" \
				eval "set -x; $ARGS"
			)
		    fi
	    esac
	done
    done

    rmdir $database.lock1
    rm $database.keyvars
    )
