
include("/common/cvs.lua")
mtn_setup()
cvs_setup()

-- This tests the case where a file was added on a branch in CVS; CVS
-- records this in a strange way (with a delete of the non-existent
-- file on mainline, followed by an add of the file on the branch).
-- Make sure we handle it correct.

mkdir("src")
writefile("src/foo", "foo")
check(indir("src", cvs("import", "-m", "import", "mod", "vtag", "rtag")), 0, false, false)
remove("src")
mkdir("src")
check(indir("src", cvs("co", "mod")), 0, false, false)
check(indir("src/mod", cvs("tag", "-b", "branch")), 0, false, false)
check(indir("src/mod", cvs("up", "-r", "branch")), 0, false, false)
writefile("src/mod/bar", "bar")
check(indir("src/mod", cvs("add", "bar")), 0, false, false)
check(indir("src/mod", cvs("ci", "-m", "add bar")), 0, false, false)

check(mtn("--branch=test", "cvs_import", cvsroot.."/mod"), 0, false, false)
