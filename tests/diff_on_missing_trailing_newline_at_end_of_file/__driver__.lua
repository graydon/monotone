--
-- Checks that "mtn diff" output produces the correct results
-- in case of a missing trailing newline at end of file.
--
-- This test goes like this:
-- 1. Commit two files: 
--    "file1": containing "foo\nbar\nquux\n"
--    "file2": containing "foo\nbar\nquux"
-- 2. Override both "file1" and "file2" with:
--    "file3": containing "foo\nbar\quux\nbaz\n"
--    "file4": containing "foo\nbar\quux\nbaz"
-- 3. Produce a "mtn diff" output and compare against:
--    "file13.diff" 
--    "file14.diff" 
--    "file23.diff" 
--    "file24.diff" 
--

mtn_setup()

check(get("file1"))
check(get("file2"))
check(get("file3"))
check(get("file4"))
check(get("file13.diff"))
check(get("file14.diff"))
check(get("file23.diff"))
check(get("file24.diff"))

addfile("file1")
addfile("file2")
commit()
anc = base_revision()

copy("file3", "file1")
check(mtn("diff", "file1"), 0, true)
canonicalize("stdout")
rename("stdout", "file13.mtn-diff")
check(samefile("file13.mtn-diff", "file13.diff"))

copy("file4", "file1")
check(mtn("diff", "file1"), 0, true)
canonicalize("stdout")
rename("stdout", "file14.mtn-diff")
check(samefile("file14.mtn-diff", "file14.diff"))

copy("file3", "file2")
check(mtn("diff", "file2"), 0, true)
canonicalize("stdout")
rename("stdout", "file23.mtn-diff")
check(samefile("file23.mtn-diff", "file23.diff"))

copy("file4", "file2")
check(mtn("diff", "file2"), 0, true)
canonicalize("stdout")
rename("stdout", "file24.mtn-diff")
check(samefile("file24.mtn-diff", "file24.diff"))

