
mtn_setup()

-- this test is kinda like update, only it *does* commit the left right
-- branch before attempting a merge. it just checks to make sure merging
-- works in the context of the "merge" command, not just the "update"
-- command.

getfile("origfile", "testfile")
addfile("testfile")
commit()
root_r_sha = base_revision()
root_f_sha = sha1("testfile")

getfile("firstinsert", "testfile")
commit()
left_r_sha = base_revision()
left_f_sha = sha1("testfile")
check(left_r_sha ~= root_r_sha)
check(left_f_sha ~= root_f_sha)

probe_node("testfile", root_r_sha, root_f_sha)

getfile("secondinsert", "testfile")
commit()
right_r_sha = base_revision()
right_f_sha = sha1("testfile")
check(right_r_sha ~= root_r_sha)
check(right_f_sha ~= root_f_sha)
check(right_r_sha ~= left_r_sha)
check(right_f_sha ~= left_f_sha)

-- now merge and update again, this time successfully
check(cmd(mtn("--branch=testbranch", "merge")), 0, false, false)
check(cmd(mtn("update")), 0, false, false)

check(cmd(mtn("--branch=testbranch", "heads")), 0, true, false)
check(not qgrep("empty", "stdout"))

-- files should now be merged
getfile("bothinsert", "probe")
check(samefile("testfile", "probe"))
