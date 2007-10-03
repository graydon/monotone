#!/bin/sh

CVSROOT=`pwd`/cvs-repository
export CVSROOT

# deleting the existing cvs-repository
rm -vrf $CVSROOT

# initializing a new repository
cvs init

# create the initial 3rd-party vendor import
mkdir test
cd test
echo "version 0 of test file1" > file1
echo "version 0 of test file2" > file2
echo "first changelog entry" > changelog
cvs import -m "Initial import of VENDORWARE 1" test VENDOR VENDOR_REL_1
cd ..
rm -rf test

# now we alter some of the files
cvs checkout test
cd test
echo "version 1 of test file1" > file1
echo "second changelog" >> changelog
cvs commit -m "commit 0"

# now we create a branch
cvs tag -b branched
cvs update -r branched

# alter the files on the branch
echo "version 2 of test file1" > file1
echo "version 1 of test file2" > file2
echo "third changelog -on branch-" >> changelog
cvs commit -m "commit on branch"

# and create some mainline changes after the branch
cvs update -A
echo "third changelog -not on branch-" >> changelog
cvs commit -m "commit on mainline after branch"

# cleanup the working directory and the CVS repository bookkeeping dir
cd ..
rm -rf test cvs-repository/CVSROOT
