
mtn_setup()

-- See makerepo.sh on how this repository was created.
check(get("cvs-repository"))

-- This tests the case where a file was added on a branch in CVS; CVS
-- records this in a strange way (with a delete of the non-existent
-- file on mainline, followed by an add of the file on the branch).
-- Make sure we handle it correct.

check(mtn("--branch=test", "cvs_import", "cvs-repository"), 0, false, false)
