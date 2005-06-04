#!/bin/sh
#
# File: mtbrowse.sh
# Description: Text based browser for monotone source control.
# url: http://www.venge.net/monotone/
# Licence: GPL
# Autor: Henry Nestler <Henry@BigFoot.de>
#
# Simple text based browser for Monotone repositories, written as shell script.
# Can select branch, revision. Views diff from revision, logs, certs and more.
# Base is dialog function and "automate" command of Monotone, with some
# sorting and grepping functionaly.
#
# To use:
#  - Copy this script into a bin PATH
#  - Run from working copy of existing project.
#    Or give full filename to database.
#  - Change your configuration
#    (Delete the "VISUAL", to use the "PAGER")
#    Please "Reload DB", if to see the new configuration
#  - Begin with menu "S Select revision"
#  - Browse in branches, revisions, diff files, view logs ....
#  - Quit menu with "Q" to save your environment.
#    Or "X" to exit without save anything.
#
# Needed tools:
#  monotone 0.19 or compatible
#  dialog (tested Version 0.9b)
#  bash, sh, ash, dash
#  less, vi or vim (use $VISUAL or $PAGER)
#  cat, cut, echo, eval, head, sort, tail, wc, xargs ...
#
# History:
# 2005/5/5 Version 0.1.1 Henry@BigFoot.de
# 
# 2005/5/9 Version 0.1.2 Henry@BigFoot.de
# Update for MT 0.19
# Diff from parent.
# Topsort or Date/Time sort, config via TOPSORT
# 
# 2005/5/13 Version 0.1.3 Henry@BigFoot.de
# Diff from 'parent' mistaken HEAD/REVISION usage.
# Limit count of revisions, change by config menu, default 20 (for big proj).
# 
# 2005/5/24 Version 0.1.4 Henry@BigFoot.de
# Don't run "monotone log" with empty head.
# 
# 2005/5/31 Version 0.1.5 Henry@BigFoot.de
# Add selection for head, if unmerged heads found.
# Short revision hash. Keys in selection. (option)
# Popup select, if more as one parent (from merge).
#
# 2005/6/1 Version 0.1.6 Henry@BigFoot.de
# Autotect for log --depth/--last, new Monotone style version 0.19+.
# Use internal dialog file viewer if VISUAL and PAGER empty.
# Config menu with radiolist. View actual setting.
# Save config only from config menu. No autosave.
# Fix cache deleting on startup.
# Xargs for revision selection with date and key.
#
# Known Bugs / ToDo-List:
# * For Monotone Version >0.19  s/--depth/--last/, remove the fallback

VERSION="0.1.6"

# Save users settings
# Default values, can overwrite on .mtbrowserc
CONFIGFILE="$HOME/.mtbrowserc"

# Store lists for menue here
TEMPDIR="$HOME/.mtbrowse"
TEMPFILE="$TEMPDIR/.tmp"

# Called with filename.
VISUAL="vim -R"

# Called with stdin redirection.
# Set VISUAL empty to use PAGER!
# Don't VISUAL and PAGER to use internal viewer.
PAGER="less"

# View date and keys in revision selection? (yes)
SHOW_KEYS="yes"

# 1=Certs Cached, 0=Clean at end (slow and save mode)
CACHE="1"

# T=Topsort revisions, D=Date sort (reverse topsort)
TOPSORT="D"

# count of certs to get from DB, "0" for all
CERTS_MAX="20"

# Trim hash code
HASH_TRIM="10"

# "log --depth=n" was changed to "log --last=n", see
# Author: joelwreed@comcast.net,  Date: 2005-05-30T00:15:27
# Autodetect this and fallback for a while.
# TODO: Remove in future
DEPTH_LAST="--last"

# read saved settings
if [ -f $CONFIGFILE ]
then
    . $CONFIGFILE
fi

# exist working copy?
if [ -f MT/options ]
then
    # Read parameters from file
    #  branch "mtbrowse"
    #database "/home/hn/mtbrowse.db"
    #     key ""

    eval `cat MT/options | sed -n -r \
      -e 's/^[ ]*(branch) \"([^\"]+)\"$/\1=\2/p' \
      -e 's/^[ ]*(database) \"([^\"]+)\"$/\1=\2/p'`

    if [ -n "$database" ]
    then
	DB=$database
	BRANCH=$branch
    fi
fi


# Simple program args supported
if [ -n "$1" ]
then
    case $1 in
      --version)
        echo "mtbrowse $VERSION"
      ;;
      --help|-h)
        echo "mtbrowse [dbfile]"
      ;;
      *)
	# Databasefile from command line
	DB="$1"
	unset BRANCH

	# MT change the options, if you continue with other DB here!
	if [ -f MT/options ]
	then
	    echo -e "\n**********\n* WARNING!\n**********\n"
	    echo "Your MT/options will be overwrite, if"
	    echo "continue with different DB file or branch"
	    echo "in exist working directory!"
	    echo -e "\nENTER to confirm  / CTRL-C to abbort"
	    read junk
	fi
      ;;
    esac
fi


# Clear cached files
do_clear_cache()
{
    rm -f $TEMPFILE.agraph $TEMPFILE.certs.$BRANCH \
      $TEMPFILE.changelog.$BRANCH
}


# clear temp files
do_clear_on_exit()
{
    rm -f $TEMPFILE.dlg-branches $TEMPFILE.agraph \
      $TEMPFILE.action-select $TEMPFILE.menu $TEMPFILE.input

    if [ "$CACHE" != "1" ]
    then
	do_clear_cache
    fi
}


# View any file (with vim, less or dialog)
do_pager()
{
    if [ -n "$VISUAL" ]
    then
	$VISUAL $1
    elif [ -n "$PAGER" ]
    then
	$PAGER < $1
    else
#	dialog --textbox $1 20 75
	dialog --textbox $1 0 0
    fi
    rm $1
}


# Add the date and user-key to the list of revisions
fill_date_key()
{
	local in_file=$1
	local out_file=$2
	local short_hash date key

	rm -f $out_file
	if [ "$SHOW_KEYS" = "yes" ]
	then
	    # Read Key and Date value from certs
	    cat $in_file | \
	    while read hash ; do
		echo -n "."

		# Scanning for
		# Key   : henry@bigfoot.de		<<---
		# Sig   : ok
		# Name  : date
		# Value : 2005-05-31T22:29:50		<<---

		# Output
		# 123456 "2005-05-31T22:29:50 henry@bigfoot.de"

		eval `monotone --db=$DB list certs $hash | \
		  tail -n 6 | sed -n -r \
		  -e 's/Key   : ([^ ]+\@[^ ]+)$/key=\1/p' \
		  -e 's/Value : ([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:]{8})$/date=\1/p'`
		short_hash=`echo $hash | cut -c 1-$HASH_TRIM`
		echo "$short_hash \"$date $key\"" >> $out_file
	    done
	else
	    # Read only Date value from certs
	    cat $in_file | \
	    while read hash ; do
		echo -n "."
		monotone --db=$DB list certs $hash | sed -n -r -e \
		 "s/Value : ([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:]{8})\$/$hash \"\1\"/p" \
		 | tail -n 1 >> $out_file
	    done
	fi
}


# Select a branch
# Is parameter given: No user select, if branch known.
do_branch_sel()
{
    # is Branch set, than can return
    if [ -n "$BRANCH" -a -n "$1" ]
    then
	return
    fi

    # New DB?
    if [ "$DB" != "`cat $TEMPFILE.fname`" ]
    then
	echo "$DB" > $TEMPFILE.fname
	unset BRANCH
    fi

    OLD_BRANCH=$BRANCH

    # Get branches from DB
    if [ ! -f $TEMPFILE.dlg-branches -o $DB -nt $TEMPFILE.dlg-branches \
	-o "$CACHE" != "1" ]
    then
	monotone --db=$DB list branches \
	 | sed -n -r -e 's/^(.+)$/\1\t-/p' > $TEMPFILE.dlg-branches \
	 || exit 200
    fi

    dialog --begin 1 2 --menu "Select branch" 0 0 0 \
      `cat $TEMPFILE.dlg-branches` 2> $TEMPFILE.input
    BRANCH=`cat $TEMPFILE.input`

    # Clear Head, if branch changed
    if [ "$OLD_BRANCH" != "$BRANCH" ]
    then
	# Clear cached files
	do_clear_cache
	do_clear_on_exit
	unset HEAD
	unset SHORT_HEAD
    fi
}


# Get head from DB (need for full log)
# Is parameter given: No user select, if head known.
do_head_sel()
{
    # is Head set, than can return
    if [ -n "$HEAD" -a -n "$1" ]
    then
	return
    fi

    monotone --db=$DB automate heads $BRANCH > $TEMPFILE.heads 2>/dev/null
    # Only one head ?
    if [ `wc -l < $TEMPFILE.heads` -eq 1 -a -n "$1" ]
    then
	HEAD=`cat $TEMPFILE.heads | head -n 1`
    else
	# List heads with autor and date. Select by user.
	monotone --db=$DB heads --branch=$BRANCH \
	  | sed -n -r -e 's/^([^ ]+) ([^ ]+) ([^ ]+)$/\1 \"\2 \3\"/p' \
	  | xargs dialog --begin 1 2 --menu "Select head" 0 0 0 \
	  2> $TEMPFILE.input
	HEAD=`cat $TEMPFILE.input`
    fi

    # trim for some outputs
    SHORT_HEAD=`echo $HEAD | cut -c 1-$HASH_TRIM`

    rm -f $TEMPFILE.heads
#    do_clear_cache
}


# User menu for current branch
do_action_sel()
{
    # Action-Menu
    while dialog --menu "Action for $REVISION" 0 60 0 \
	"L" "Log view of current revision" \
	"P" "Diff files from parent" \
	"W" "Diff files from working copy head" \
	"S" "Diff files from selected revision" \
	"C" "List Certs" \
	"F" "List changed file revision" \
	"-" "-" \
	"Q" "Return" \
	2> $TEMPFILE.action-select
    do

	case `cat $TEMPFILE.action-select` in
	  L)
	    # LOG
	    # 0.19   monotone log --depth=n id file
	    # 0.19+  monotone log --last=n id file
	    if ! monotone --db=$DB log $DEPTH_LAST=1 --revision=$REVISION \
	      > $TEMPFILE.change.log
	    then
		DEPTH_LAST="--depth"
		dialog --title "ERROR" --msgbox \
		  "Fallback to \"$DEPTH_LAST\" usage.\nPlease try again." 6 40
	    else
		do_pager $TEMPFILE.change.log
	    fi
	    ;;
	  P)
	    # DIFF parent
	    monotone automate parents $REVISION > $TEMPFILE.parents

	    if [ `wc -l < $TEMPFILE.parents` -ne 1 ]
	    then
		# multiple parents (from merge)

		# Set DATE/KEY information
		fill_date_key $TEMPFILE.parents $TEMPFILE.certs3tmp

		cat cat $TEMPFILE.certs3tmp | xargs dialog --begin 1 2 --menu \
		  "Select parent for $REVISION" 0 0 0 \
		  2> $TEMPFILE.input \
		  && PARENT=`cat $TEMPFILE.input`
	    else
		# Single parent only
		PARENT=`cat $TEMPFILE.parents`
	    fi
	    rm $TEMPFILE.parents

	    if [ -z "$PARENT" ]
	    then
		dialog --msgbox "No parent found\n$REVISION" 6 45
	    else
		monotone --db=$DB diff \
		  --revision=$PARENT --revision=$REVISION \
		  > $TEMPFILE.parent.diff || exit 200
		do_pager $TEMPFILE.parent.diff
	    fi
	    ;;
	  W)
	    # DIFF
	    # monotone diff --revision=id
	    if [ "$HEAD" = "$REVISION" ]
	    then
		dialog --msgbox "Can't diff with head self\n$HEAD" 6 45
	    else
		# exist working copy?
		if [ -f MT/options ]
		then
		    monotone --db=$DB diff \
		      --revision=$REVISION \
		      > $TEMPFILE.cwd.diff || exit 200
		else
		    # w/o MT dir don't work:
		    # Help MT with HEAD info ;-)
		    monotone --db=$DB diff \
		      --revision=$HEAD --revision=$REVISION \
		      > $TEMPFILE.cwd.diff || exit 200
		fi
		do_pager $TEMPFILE.cwd.diff
	    fi
	    ;;
	  S)
	    # DIFF2: from other revision (not working dir)
	    # Select second revision
	    if cat $TEMPFILE.certs.$BRANCH | \
	      xargs dialog --menu \
	      "Select _older_ revision for branch:$BRANCH\nrev:$REVISION" \
	      0 0 0  2> $TEMPFILE.revision-select
	    then
		REV2=`cat $TEMPFILE.revision-select`

		# monotone diff --revision=id1 --revision=id2
		monotone --db=$DB diff \
		  --revision=$REV2 --revision=$REVISION \
		  > $TEMPFILE.ref.diff || exit 200
		do_pager $TEMPFILE.ref.diff
	    fi
	    rm -f $TEMPFILE.revision-select
	    ;;
	  C)
	    # List certs
	    monotone --db=$DB list certs $REVISION > $TEMPFILE.certs.log \
	      || exit 200
	    do_pager $TEMPFILE.certs.log
	    ;;
	  F)
	    # List changed files
	    monotone --db=$DB cat revision $REVISION > $TEMPFILE.rev.changed \
	      || exit 200
	    do_pager $TEMPFILE.rev.changed
	    ;;
	  Q)
	    # Menu return
	    return
	    ;;
	esac
    done
}


# Select a revision
do_revision_sel()
{
    # if branch or head not known, ask user
    do_branch_sel check
    do_head_sel check

    # Building revisions list
    if [ ! -f $TEMPFILE.certs.$BRANCH -o $DB -nt $TEMPFILE.certs.$BRANCH ]
    then
	echo "Reading ancestors ($HEAD)"
	echo "$HEAD" > $TEMPFILE.ancestors
	monotone automate ancestors $HEAD | cut -c 1-40 \
	  >> $TEMPFILE.ancestors || exit 200

	if [ "$TOPSORT" = "T" -o "$CERTS_MAX" -gt 0 ]
	then
		echo "Topsort..."
		monotone automate toposort `cat $TEMPFILE.ancestors` \
		  > $TEMPFILE.topsort || exit 200

		if [ "$CERTS_MAX" -gt 0 ]
		then
			# Only last certs. Remember: Last line is newest!
			tail -n "$CERTS_MAX" < $TEMPFILE.topsort \
			  > $TEMPFILE.topsort2
			mv $TEMPFILE.topsort2 $TEMPFILE.topsort
		fi
	else
		mv $TEMPFILE.ancestors $TEMPFILE.topsort
	fi

	# Reading revisions and fill with date
	echo -n "Reading certs"
	fill_date_key $TEMPFILE.topsort $TEMPFILE.certs3tmp

	if [ "$TOPSORT" != "T" ]
	then
		# Sort by date+time
		sort -k 2 -r < $TEMPFILE.certs3tmp > $TEMPFILE.certs.$BRANCH
		rm $TEMPFILE.certs3tmp
	else
		mv $TEMPFILE.certs3tmp $TEMPFILE.certs.$BRANCH
	fi
    fi

    # Select revision
    while cat $TEMPFILE.certs.$BRANCH | \
	xargs dialog --menu "Select revision for branch:$BRANCH" \
	0 0 0  2> $TEMPFILE.revision-select
    do

	REVISION=`cat $TEMPFILE.revision-select`

	# Remove old marker, set new marker
	cat $TEMPFILE.certs.$BRANCH | sed -r \
	  -e "s/^(.+) <==\"\$/\1\"/" -e "s/^($REVISION.+)\"\$/\1 <==\"/" \
	  > $TEMPFILE.certs.$BRANCH.base
	mv $TEMPFILE.certs.$BRANCH.base $TEMPFILE.certs.$BRANCH

	# Error, on "monotone automate parent XXXXXX".  :-(
	# Expand revision here, if short revision
	if [ "$SHOW_KEYS" = "yes" ]
	then
	    REVISION=`monotone complete revision $REVISION`
	fi

	# OK Button: Sub Menu
	do_action_sel
    done
    rm -f $TEMPFILE.revision-select
}


# Menu for configuration
do_config()
{
    local y n

    while dialog --menu "Configuration" 0 0 0 \
	"V" "VISUAL [$VISUAL]" \
	"Vd" "Set VISUAL default to vim -R" \
	"P" "PAGER  [$PAGER]" \
	"Pd" "set PAGER default to less" \
	"K" "View KEY in selection [$SHOW_KEYS]" \
	"S" "Sort by Topsort or Date [$TOPSORT]" \
	"C" "Certs limit in Select-List [$CERTS_MAX]" \
	"-" "-" \
	"W" "Write configuration file" \
	"R" "Return to main menu" \
	2> $TEMPFILE.menu
    do
	case `cat $TEMPFILE.menu` in
	  V)
	    # Setup for VISUAL
	    dialog --inputbox \
	     "Config for file viewer\nused in sample \"vim -R changes.diff\"" \
	     8 70 "$VISUAL" 2> $TEMPFILE.input \
	     && VISUAL=`cat $TEMPFILE.input`
	    ;;
	  Vd)
	    # set Visual default
	    VISUAL="vim -R"
	    ;;
	  P)
	    # Setup for PAGER
	    dialog --inputbox \
	     "Config for pipe pager\nused in sample \"monotone log | less\"" \
	     8 70 "$PAGER" 2> $TEMPFILE.input \
	     && PAGER=`cat $TEMPFILE.input`
	    ;;
	  Pd)
	    # set Pager default
	    PAGER="less"
	    ;;
	  K)
	    # change yes=View Keys, no=date only

	    if [ "$SHOW_KEYS" = "yes" ]; then
		y="on" n="off"
	    else
		y="off" n="on"
	    fi

	    dialog --radiolist "View Keys in Selection?" 0 0 0 \
	      "yes" "Short revision, date and keys" $y \
	      "no" "Long revision and Date" $n \
	      2> $TEMPFILE.input \
	      && SHOW_KEYS=`cat $TEMPFILE.input`
	    ;;
	  S)
	    # change T=Topsort revisions, D=Date sort (reverse topsort)
	    if [ "$TOPSORT" = "T" ]; then
		y="on" n="off"
	    else
		y="off" n="on"
	    fi

	    dialog --radiolist "Sort revisions by" 0 0 0 \
	      "D" "Date/Time (reverse topsort)" $n \
	      "T" "Topsort (from Monotone)" $y \
	      2> $TEMPFILE.input \
	      && TOPSORT=`cat $TEMPFILE.input`
	    ;;
	  C)
	    # Change CERTS_MAX
	    dialog --inputbox \
	      "Set maximum lines for revision selction menu\n(default: 0, disabled)" \
	      9 70 "$CERTS_MAX" 2> $TEMPFILE.input \
	      && CERTS_MAX=`cat $TEMPFILE.input`
	    ;;
	  W)
	    # Save environment
	    cat > $CONFIGFILE << EOF
# File: ~/.mtbrowserc

DB="$DB"
BRANCH="$BRANCH"
VISUAL="$VISUAL"
PAGER="$PAGER"
TEMPDIR="$TEMPDIR"
TEMPFILE="$TEMPFILE"
SHOW_KEYS="$SHOW_KEYS"
TOPSORT="$TOPSORT"
CACHE="$CACHE"
CERTS_MAX="$CERTS_MAX"
DEPTH_LAST="$DEPTH_LAST"
EOF
		dialog --title "Info" --sleep 2 --infobox \
		    "Configration wrote to\n$CONFIGFILE" 0 0
	    echo "config saved"
    	    ;;
	  *)
	    # Return to Main
	    rm -f $TEMPFILE.input
	    return
	    ;;
	esac
    done
}

# Is dialog installed?
if ! dialog --version </dev/null >/dev/null 2>&1
then
    # Hm, need this here
    echo
    echo "dialog - display dialog boxes from shell scripts."
    echo "Dialog is needed for this tool, please install it!"
    echo
    exit -1
fi

mkdir -p $TEMPDIR

while dialog --menu "Main - mtbrowse v$VERSION" 0 0 0 \
    "S" "Select revision" \
    "I" "Input revision" \
    "F" "Change DB File [`basename $DB`]" \
    "B" "Branch select  [$BRANCH]" \
    "H" "Head select    [$SHORT_HEAD]" \
    "R" "Reload DB, clear cache" \
    "-" "-" \
    "l" "Sumary complete log" \
    "t" "List Tags" \
    "h" "List Heads" \
    "k" "List Keys" \
    "-" "-" \
    "C" "Configuration" \
    "-" "-" \
    "X" "eXit" \
    2> $TEMPFILE.menu
do
    case `cat $TEMPFILE.menu` in
      S)
	# Revision selection
	do_revision_sel
	;;
      I)
	# Input Revision
	if dialog --inputbox \
	  "Input 5 to 40 digits of known revision" 8 60 "$REVISION" \
	  2> $TEMPFILE.input
	then
	    REVISION=`cat $TEMPFILE.input`

	    do_action_sel
	    do_revision_sel
	fi
	;;
      R)
	# Cache del and Revision selection
	do_clear_cache
	do_revision_sel
	;;
      B)
	# Branch config
	rm -f $TEMPFILE.dlg-branches
	do_branch_sel
	;;
      H)
        # Select head
	do_head_sel
	do_clear_cache
	;;
      F)
	# Change DB file
	DNAME=`dirname $DB`
	if [ -z "$DNAME" ]
	then
	    DNAME=`pwd`
	fi
	
	if dialog --fselect $DNAME/`basename $DB` 15 70 2> $TEMPFILE.name-db
	then
	    DB=`cat $TEMPFILE.name-db`
	    dialog --msgbox "file changed to\n$DB" 0 0
	    unset BRANCH
	else
	    dialog --msgbox "filename unchanged" 0 0
	fi
	rm -f $TEMPFILE.name-db
	;;
      C)
	do_config
	# Clear cache
	do_clear_cache
	;;
      l)
	# Sumary coplete LOG
	# if not branch known, ask user
	do_branch_sel check
	do_head_sel check

	if [ ! -f $TEMPFILE.changelog.$BRANCH -o \
	    $DB -nt $TEMPFILE.changelog.$BRANCH ]
	then
	    echo "Reading log...($BRANCH)"
	    monotone --db=$DB log --revision=$HEAD > $TEMPFILE.changelog.$BRANCH || exit 200
	fi
	cp $TEMPFILE.changelog.$BRANCH $TEMPFILE.change.log
	do_pager $TEMPFILE.change.log
	;;
      t)
	# List Tags
	echo "Reading Tags..."
	monotone --db=$DB list tags > $TEMPFILE.tags.log || exit 200
	do_pager $TEMPFILE.tags.log
	;;
      h)
	# if not branch known, ask user
	do_branch_sel check

	monotone --db=$DB heads --branch=$BRANCH > $TEMPFILE.txt || exit 200
	do_pager $TEMPFILE.txt
	;;
      k)
	# List keys
	monotone --db=$DB list keys > $TEMPFILE.txt || exit 200
	do_pager $TEMPFILE.txt
	;;
      X)
	do_clear_on_exit
	exit 0
        ;;
      *)
	echo "Error in Menu!"
	exit 250
        ;;
    esac
done

do_clear_on_exit
