
mtn_setup()

-- this test is kinda like update, only it *does* commit the left right
-- branch before attempting a merge. it just checks to make sure merging
-- works in the context of the "merge" command, not just the "update"
-- command.

check(get("origfile", "testfile"))
addfile("testfile")
commit()
root_r_sha = base_revision()
root_f_sha = sha1("testfile")

check(get("firstinsert", "testfile"))
commit()
left_r_sha = base_revision()
left_f_sha = sha1("testfile")
check(left_r_sha ~= root_r_sha)
check(left_f_sha ~= root_f_sha)

probe_node("testfile", root_r_sha, root_f_sha)

check(get("secondinsert", "testfile"))
commit()
right_r_sha = base_revision()
right_f_sha = sha1("testfile")
check(right_r_sha ~= root_r_sha)
check(right_f_sha ~= root_f_sha)
check(right_r_sha ~= left_r_sha)
check(right_f_sha ~= left_f_sha)

-- now merge and update again, this time successfully
check(mtn("--branch=testbranch", "merge"), 0, false, false)
check(mtn("update"), 0, false, false)

check(mtn("--branch=testbranch", "heads"), 0, true, false)
check(not qgrep("empty", "stdout"))

-- files should now be merged
check(get("bothinsert", "probe"))
check(samefile("testfile", "probe"))
