
mtn_setup()

-- This test relies on file-suturing

--
-- If two people begin a branch with the same name,
-- then the branch does not have a unique root.  If they
-- also happened to both add a file with some shared and
-- some differing lines, we must do the right thing.
--

writefile("foo.left",  "z\na\nb\nx\n")
writefile("foo.right", "z\nj\nk\nx\n")

copy("foo.left", "foo")
check(mtn("add", "foo"), 0, false, false)
commit()
left = base_revision()

remove("foo")
remove("_MTN")
check(mtn("setup", "--branch=testbranch", "."))

copy("foo.right", "foo")
check(mtn("add", "foo"), 0, false, false)
commit()
right = base_revision()

check(get("merge2.lua"))

xfail_if(true, mtn("--rcfile=merge2.lua", "merge"), 0, false, false)
check(mtn("update"), 0, false, false)
merge = base_revision()

--
-- annotate foo should now be
-- REVC: z
-- REVL: a
-- REVR: k
-- REVC: x
--
-- where REVC (choice) is either REVL or REVR

check(mtn("annotate", "--brief", "foo"), 0, true, false)
check(greplines("stdout", {"", left, right, ""}))
