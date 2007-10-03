#!/bin/sh

CVSROOT=`pwd`/cvs-repository
export CVSROOT

# deleting the existing cvs-repository
rm -vrf $CVSROOT

# initializing a new repository
cvs init

# add a file 'foo'
mkdir full_checkout
cd full_checkout
cvs co .
mkdir test
echo "version 0 of test file foo" > test/foo
cvs add test
cvs add test/foo
cvs commit -m "commit 0" test/foo
cd ..
rm -rf full_checkout

# clean up the CVS repository bookkeeping dir
rm -rf cvs-repository/CVSROOT
