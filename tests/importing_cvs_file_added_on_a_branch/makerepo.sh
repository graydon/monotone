#!/bin/sh

CVSROOT=`pwd`/cvs-repository
export CVSROOT

# deleting the existing cvs-repository
rm -vrf $CVSROOT

# initializing a new repository
cvs init

# create an initial module 'test' with one file 'foo'
mkdir test
cd test
echo "foo" > foo
cvs import -m "import" test vtag rtag
cd ..
rm -rf test

# create a branch 'branch'
cvs co test
cd test
cvs tag -b branch
cvs update -r branch

# then add a file 'bar'
echo "bar" > bar
cvs add bar
cvs commit -m "add bar"
cd ..
rm -rf test

# clean up the CVS repository bookkeeping dir
rm -rf cvs-repository/CVSROOT
