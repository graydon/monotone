#! /bin/sh
#
# Reads a simple specification in the following form:
#
#	DIRECTORY	BRANCH
#
# and updates each directory with the data from said branch.
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
#	Default: /etc/monotone/update.rc

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
    rc=/etc/monotone/update.rc
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

sed -e '/^#/d' < "$rc" | while read DIRECTORY BRANCH; do
    if [ -n "$DIRECTORY" -o -n "$BRANCH" ]; then
	if [ -z "$DIRECTORY" -o -z "$BRANCH" ]; then
	    echo "Directory or branch missing in line: $DIRECTORY $BRANCH" >&2
	    echo "Skipping..." >&2
	elif [ -d "$DIRECTORY" ]; then
	    (
		if [ -d $DIRECTORY/_MTN ]; then
		    thisbranch=
		    if [ -f $DIRECTORY/_MTN/options ]; then
			thisbranch=`grep '^ *branch ' $DIRECTORY/_MTN/options | sed -e 's/^ *branch *"//' -e 's/" *$//'`
		    fi
		    if [ "$thisbranch" = "$BRANCH" ]; then
			echo "Updating the directory $DIRECTORY" >&2
			( cd $DIRECTORY; mtn update )
		    else
			echo "The directory $DIRECTORY doesn't contain the branch $BRANCH" >&2
			echo "Skipping..." >&2
		    fi
		else
		    filesn=`ls -1 -a $DIRECTORY | egrep -v '^\.\.?$' | wc -l`
		    if [ "$filesn" -eq 0 ]; then
			echo "Extracting branch $BRANCH into empty directory $DIRECTORY" >&2
			( cd $DIRECTORY; mtn -d "$database" -b "$BRANCH" co . )
		    else
			
			echo "The directory $DIRECTORY doesn't contain the branch $BRANCH" >&2
			echo "Skipping..." >&2
		    fi
		fi
	    )
	elif [ -e "$DIRECTORY" ]; then
	    echo "There is a file $DIRECTORY, but it's not a directory" >&2
	    echo "Skipping..." >&2
	else
	    echo "Extracting branch $BRANCH into directory $DIRECTORY" >&2
	    mtn -d "$database" -b "$BRANCH" co "$DIRECTORY"
	fi
    fi
done
