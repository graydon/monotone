#!/bin/sh

CVSROOT=`pwd`/cvs-repository
export CVSROOT

# deleting the existing cvs-repository
rm -vrf $CVSROOT

# initializing a new repository
cvs init

# do a full checkout of the repository
mkdir full_checkout
cd full_checkout
cvs co .
mkdir test
cvs add test

# do some commits on file foo
echo "version 0 of test file foo" > test/foo
cvs add test/foo
cvs commit -m "commit 0" test/foo

echo "version 1 of test file foo" > test/foo
cvs commit -m "commit same message" test/foo

echo "version 2 of test file foo" > test/foo
cvs commit -m "commit same message" test/foo

echo "version 3 of test file foo" > test/foo
cvs commit -m "commit 3" test/foo

cd ..
rm -rf full_checkout

# clean up the CVS repository bookkeeping dir
rm -rf cvs-repository/CVSROOT
