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

cd test

# an initial import
echo "version 0 of test file1" > file1
echo "version 0 of test file2" > file2
echo "first changelog entry" > changelog
cvs add file1 file2 changelog
cvs commit -m "initial import" file1 file2 changelog

# commit first changes
echo "version 1 of test file1" > file1
echo "second changelog" >> changelog
cvs commit -m "first commit" file1 changelog

# now we create a branch
cvs tag -b branched
cvs update -r branched

# alter the files on the branch
echo "version 1 of test file2" > file2
echo "third changelog -on branch-" >> changelog
cvs commit -m "commit on branch" file2 changelog

# switch to the HEAD branch
cvs update -A

# do a commit on the HEAD
echo "version 2 of test file1" > file1
echo "third changelog -not on branch-" >> changelog
cvs commit -m "commit on mainline after branch" file1 changelog

cd ../..
rm -rf full_checkout

# clean up the CVS repository bookkeeping dir
rm -rf cvs-repository/CVSROOT
