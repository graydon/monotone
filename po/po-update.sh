#!/bin/sh

#
# Original Source taken from the Gnome project:
# http://mail.gnome.org/archives/gnome-de/2000-October/msg00008.html
#
# Adapted to match the xgettext options used by monotone.
#
##
# Invoke this script from the `po' directory.
# As arguments enter the message catalog (language code) you
# want to update or to create.
#
# Usage:
#
#    ./po-update.sh fr
#
# to update or create the French (fr) message catalog ready for
# starting the translation.
#
# Have a lot of fun...

export LANGUAGE=C
export LC_ALL=C

CATALOGS=$@
PACKAGE=monotone

xgettext --keyword=F --keyword=FP:1,2 --keyword=_ --keyword=N_ \
    --flag=F:1:c-format --flag=FP:1:c-format \
    --flag=FP:2:c-format \
    --directory=.. --files-from=./POTFILES.in --output=$PACKAGE.pot

if [ -n "$CATALOGS" ] ; then
  for c in $CATALOGS ; do
    if [ -r $c.po ] ; then
      mv $c.po $c.po.old
      msgmerge $c.po.old $PACKAGE.pot -o $c.po
      echo "Checking $c.po of package $PACKAGE..."
      msgfmt -o /dev/null -c -v --statistics $c.po
      echo "...done."
    else
      cp $PACKAGE.pot $c.po
      echo "$PACKAGE has a new message file: $c.po"
    fi
  done
fi

exit 0
