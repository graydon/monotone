
mtn_setup()

writefile("foo", "blah blah")
-- The UI used to fix these, while later code did not, so let's check
-- the inner code directly.
writefile("_MTN/work", 'add_dir "."')
check(mtn("automate", "get_manifest_of"), 3, false, false)

writefile("_MTN/work", 'add_dir "./bar"')

check(mtn("automate", "get_manifest_of"), 3, false, false)
check(mtn("automate", "get_revision"), 3, false, false)
check(mtn("commit", "--message=foo", "--branch=foo"), 3, false, false)
