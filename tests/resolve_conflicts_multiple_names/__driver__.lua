-- Test resolving multiple names conflicts
--
-- this is currently not supported; we are documenting a test case
-- that must be considered when implementing it.

mtn_setup()

-- two conflicts on single node; what does 'conflicts resolve_first' do?

addfile("foo", "foo base")
commit("testbranch", "base")
base = base_revision()

writefile("foo", "foo left")
check(mtn("mv", "foo", "bar"), 0, false, false)

commit("testbranch", "left 1")
left_1 = base_revision()

revert_to(base)

writefile("foo", "foo right")
check(mtn("mv", "foo", "baz"), 0, false, false)
commit("testbranch", "right 1")
right_1 = base_revision()

check(mtn("conflicts", "store"), 0, nil, true)
canonicalize("stderr")
check("mtn: warning: 1 conflict with no supported resolutions.\n" == readfile("stderr"))

-- mtn("conflicts", "show_first")
-- end of file
