
mtn_setup()

-- this test is kinda like fork, only it doesn't commit the right side of
-- the fork; instead, it updates, and (in theory) shifts from right to
-- merged-with-left

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

check(mtn("update"), 0, false, false)

-- files should now be merged

getfile("bothinsert", "probe")

check(samefile("testfile", "probe"))
