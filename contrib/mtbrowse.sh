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
#    Or X to exit without save anything.
#
# Needed tools:
#  monotone 0.19 or compatible
#  dialog (tested Version 0.9b)
#  bash, sh, ash, dash
#  less, vi or vim (use $VISUAL or $PAGER)
#  cat, cut, echo, head, tail, sort ...
#
# History:
# 2005/5/5 Version 0.1.1 Henry@BigFoot.de
# 
# 2005/5/9 Version 0.1.2 Henry@BigFoot.de
# Update for MT 0.19
# Diff from parrent.
# Topsort or Date/Time sort, config via TOPSORT
# 
# 2005/5/13 Version 0.1.3 Henry@BigFoot.de
# Diff from 'Parrent' mistaken HEAD/REVISION usage.
# Limit count of revisions, change by config menu, default 20 (for big proj).
#
# Known Bugs / ToDo-List:
# - Working without data from "checkout" don't work in all cases.
#   "diff --revision=older --revision=newer" produce a MT bug (with renamed files).
# - Merged revision list only with one parrent.
# - Two heads list only as one head.

# Save users settings
# Default values, can overwrite on .mtbrowserc
CONFIGFILE="$HOME/.mtbrowserc"

# Store lists for menue here
TEMPDIR="$HOME/.mtbrowse"
TEMPFILE="$TEMPDIR/.tmp"

# Called with filename.
VISUAL="vim -R"

# Called with stdin redirection. Set VISUAL empty to use PAGER!
PAGER="less"

# 1=Certs Cached, 0=Clean at end (slow and save mode)
CACHE="1"

# 1=Topsort revisions, 0=Date sort (reverse topsort)
TOPSORT="0"

# count of certs to get from DB, "0" for all
CERTS_MAX="20"

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

# Databasefile from command line
if [ -n "$1" ]
then
    DB="$1"
    unset BRANCH

    # MT change the options, if you continue with other DB here!
    if [ -f MT/options ]
    then
	    echo -e "\n**********\n* WARNING!\n**********\n"
	    echo "Your MT/options will be overwrite, if"
	    echo "continue with different DB file or branch"
	    echo "in exist woring directory!"
	    echo -e "\nENTER to confirm  / CTRL-C to abbort"
	    read junk
    fi
fi

do_clear_cache()
{
    rm -f $TEMPFILE.agraph $TEMPFILE.certs.$BRANCH \
      $TEMPFILE.changelog.$BRANCH
}

do_clear_on_exit()
{
    rm -f $TEMPFILE.dlg-branches $TEMPFILE.agraph \
      $TEMPFILE.action-select $TEMPFILE.menu

    if [ "$CACHE" != "1" ]
    then
	do_clear_cache
    fi
}

do_pager()
{
    if [ -n "$VISUAL" ]
    then
	$VISUAL $1
    else
	$PAGER < $1
    fi
    rm $1
}


do_branch_sel()
{
    # is Branch set, than can return
    if [ -n $BRANCH -a -n "$1" ]
    then
	return
    fi

    # New DB?
    if [ "$DB" != "`cat $TEMPFILE.fname`" ]
    then
	# Clear cached files
	do_clear_cache
	do_clear_on_exit
	echo "$DB" > $TEMPFILE.fname
    fi

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
    rm -f $TEMPFILE.input
}


do_head_sel()
{
    # Get head from DB (need for full log)
    if [ -z "$HEAD" ]
    then
	HEAD=`monotone --db=$DB automate heads $BRANCH | head -n 1`
    fi
}


do_action_sel()
{
    # Action-Menu
    while dialog --menu "Action for $REVISION" 0 60 0 \
	"L" "Log view of actual revision" \
	"P" "Diff files from parrent" \
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
	    # monotone log --depth=n id file
	    monotone --db=$DB log --depth=1 --revision=$REVISION \
	      > $TEMPFILE.change.log || exit 200
	    do_pager $TEMPFILE.change.log
	    ;;
	  P)
	    # DIFF Parrent
	    PARRENT=`monotone automate parents $REVISION | head -n 1`
	    if [ -z "$PARRENT" ]
	    then
		dialog --msgbox "No parrent found\n$HEAD" 6 45
	    else
		monotone --db=$DB diff \
		  --revision=$PARRENT --revision=$REVISION \
		  > $TEMPFILE.parrent.diff || exit 200
		do_pager $TEMPFILE.parrent.diff
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
	    if dialog --menu \
	      "Select _older_ revision for branch:$BRANCH\nrev:$REVISION" \
	      0 0 0  `cat $TEMPFILE.certs.$BRANCH` \
	      2> $TEMPFILE.revision-select
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

do_revision_sel()
{
    # if branch not known, ask user
    do_branch_sel check
    do_head_sel check

    # Building revisions list
    if [ ! -f $TEMPFILE.certs.$BRANCH -o $DB -nt $TEMPFILE.certs.$BRANCH ]
    then
	echo "Reading ancestors ($HEAD)"
#set -x xtrace
	echo "$HEAD" > $TEMPFILE.ancestors
	monotone automate ancestors $HEAD | cut -c0-40 >> $TEMPFILE.ancestors || exit 200

	if [ "$TOPSORT" = "1" -o "$CERTS_MAX" -gt 0 ]
	then
		echo "Topsort..."
		monotone automate toposort `cat $TEMPFILE.ancestors` > $TEMPFILE.topsort || exit 200

		if [ "$CERTS_MAX" -gt 0 ]
		then
			# Only last certs. Remember: Last line is newest!
			tail -n "$CERTS_MAX" < $TEMPFILE.topsort > $TEMPFILE.topsort2
			mv $TEMPFILE.topsort2 $TEMPFILE.topsort
		fi
	else
		mv $TEMPFILE.ancestors $TEMPFILE.topsort
	fi

	rm -f $TEMPFILE.certs3tmp
	echo -n "Reading certs"
	cat $TEMPFILE.topsort | \
	while read hash ; do
		echo -n "."
		echo -n "$hash " >> $TEMPFILE.certs3tmp
		monotone --db=$DB list certs $hash | sed -n -r -e \
		  's/Value : ([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:]{8})$/\1/p' \
		  | tail -n 1 >> $TEMPFILE.certs3tmp
	done

	if [ "$TOPSORT" != "1" ]
	then
		# Sort by date+time
		sort -k 2 -r < $TEMPFILE.certs3tmp > $TEMPFILE.certs.$BRANCH
	else
		mv $TEMPFILE.certs3tmp $TEMPFILE.certs.$BRANCH
	fi
#echo -e "\nDONE"
#read junk
#set +x xtrace
    fi

    # Select revision
    while dialog --menu "Select revision for branch:$BRANCH" \
	0 0 0 `cat $TEMPFILE.certs.$BRANCH` 2> $TEMPFILE.revision-select
    do

	REVISION=`cat $TEMPFILE.revision-select`

	# Remove old marker, set new marker
	cat $TEMPFILE.certs.$BRANCH \
	  | sed -r -e "s/^(.+)###\$/\1/" -e "s/^($REVISION.+)\$/\1###/" \
	  > $TEMPFILE.certs.$BRANCH.base
	mv $TEMPFILE.certs.$BRANCH.base $TEMPFILE.certs.$BRANCH

	# OK Button: Sub Menu
	do_action_sel
    done
    rm -f $TEMPFILE.revision-select
}

do_config()
{
    while dialog --menu "Configuration" 0 0 0 \
	"V" "VISUAL [$VISUAL]" \
	"vd" "Set VISUAL default to vim -R" \
	"P" "PAGER  [$PAGER]" \
	"pd" "set PAGER default to less" \
	"s" "Sort by Date (0) or Topsort (1) [$TOPSORT]" \
	"C" "Certs limit in Select-List [$CERTS_MAX]" \
	"-" "-" \
	"R" "Return to main menu" \
	2> $TEMPFILE.menu
    do
	case `cat $TEMPFILE.menu` in
	  V)
	    # Setup for VISUAL
	    dialog --inputbox \
	      "Config for file viewer (used in sample \"vim -R changes.diff\")" \
	      8 70 "$VISUAL" 2> $TEMPFILE.input \
	      && VISUAL=`cat $TEMPFILE.input`
	    rm -f $TEMPFILE.input
	    ;;
	  vd)
	    # set Visual default
	    VISUAL="vim -R"
	    ;;
	  P)
	    # Setup for PAGER
	    dialog --inputbox \
	      "Config for pipe pager (used in sample \"monotone log | less\")" \
	      8 70 "$PAGER" 2> $TEMPFILE.input \
	      && PAGER=`cat $TEMPFILE.input`
	    rm -f $TEMPFILE.input
	    ;;
	  pd)
	    # set Pager default
	    PAGER="less"
	    ;;
	  s)
	    # change 1=Topsort revisions, 0=Date sort (reverse topsort)
	    dialog --menu "Sort revisions by" 0 0 0 \
	      "0" "Date/Time (reverse topsort)" \
	      "1" "topsort (from Monotone)" \
	      2> $TEMPFILE.input \
	      && TOPSORT=`cat $TEMPFILE.input`
	    rm -f $TEMPFILE.input
	    ;;
	  C)
	    # Change CERTS_MAX
	    dialog --inputbox \
	      "Set maximum lines for revision selction menu\n(default: 0, disabled)" \
	      9 70 "$CERTS_MAX" 2> $TEMPFILE.input \
	      && CERTS_MAX=`cat $TEMPFILE.input`
	    rm -f $TEMPFILE.input
	    ;;
	  *)
	    # Return to Main
	    return
	    ;;
	esac
    done
}


mkdir -p $TEMPDIR

while dialog --menu "Main - mtbrowse v0.1.3" 0 0 0 \
    "S" "Select revision" \
    "I" "Input revision" \
    "F" "Change DB File [`basename $DB`]" \
    "B" "Branch select  [$BRANCH]" \
    "R" "Reload DB, clear cache" \
    "L" "Sumary complete log" \
    "T" "List Tags" \
    "H" "List Heads" \
    "K" "List Keys" \
    "C" "Configuration" \
    "-" "-" \
    "X" "eXit witout save" \
    "Q" "Quit, save session" \
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
	  "Input 5 to 40 digits of known revision" 8 60 "$REVISION" 2> $TEMPFILE.input
	then
	    REVISION=`cat $TEMPFILE.input`
	    rm -f $TEMPFILE.input

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
	;;
      L)
	# Sumary coplete LOG
	# if not branch known, ask user
	do_branch_sel check

	if [ ! -f $TEMPFILE.changelog.$BRANCH -o \
	    $DB -nt $TEMPFILE.changelog.$BRANCH ]
	then
	    echo "Reading log...($BRANCH)"
	    monotone --db=$DB log --revision=$HEAD > $TEMPFILE.changelog.$BRANCH || exit 200
	fi
	cp $TEMPFILE.changelog.$BRANCH $TEMPFILE.change.log
	do_pager $TEMPFILE.change.log
	;;
      T)
	# List Tags
	echo "Reading Tags..."
	monotone --db=$DB list tags > $TEMPFILE.tags.log || exit 200
	do_pager $TEMPFILE.tags.log
	;;
      H)
	# if not branch known, ask user
	do_branch_sel check

	monotone --db=$DB heads --branch=$BRANCH > $TEMPFILE.txt || exit 200
	do_pager $TEMPFILE.txt
	;;
      K)
	# List keys
	monotone --db=$DB list keys > $TEMPFILE.txt || exit 200
	do_pager $TEMPFILE.txt
	;;
      Q)
	# Quit, Save environment
	cat > $CONFIGFILE << EOF

# File: ~/.mtbrowserc

DB="$DB"
BRANCH="$BRANCH"
VISUAL="$VISUAL"
PAGER="$PAGER"
TEMPDIR="$TEMPDIR"
TEMPFILE="$TEMPFILE"
CACHE="$CACHE"
TOPSORT="$TOPSORT"
CERTS_MAX="$CERTS_MAX"
EOF
	do_clear_on_exit
	exit 0
        ;;
      X)
	do_clear_on_exit
	exit 20
        ;;
      *)
	echo "Error in Menu!"
	exit 250
        ;;
    esac
done

do_clear_on_exit
