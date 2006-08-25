
include("common/netsync.lua")
mtn_setup()
netsync.setup()

addfile("bar", "bar")
commit("otherbranch")
remove("_MTN")
check(mtn("setup", ".", "-b", "testbranch"), 0, false, false)
addfile("foo", "foo\n")
commit()
base = base_revision()

netsync.sync("*branch")
append("foo", "bar\n")
commit()
check(mtn("merge_into_dir", "otherbranch", "testbranch", "test"), 0, false, false)

check(mtn("update", "-r", base), 0, false, false)

append("foo", "baz\n")
commit(nil, nil, mtn2)
check(mtn2("merge_into_dir", "otherbranch", "testbranch", "test"), 0, false, false)

check(get("rcfile"))
check(mtn("merge", "--rcfile", "rcfile"), 0, false, false)
check(mtn2("merge", "--rcfile", "rcfile"), 0, false, false)

netsync.sync("*branch")

-- should be a conflict
check(mtn("merge"), 1, false, false)
