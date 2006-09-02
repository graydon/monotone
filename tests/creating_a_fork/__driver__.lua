
mtn_setup()

writefile("testfile", "version 0 of test file\n")

check(mtn("add", "testfile"), 0, false, false)
commit()
root_r_sha = base_revision()
root_f_sha = sha1("testfile")

writefile("testfile", "left version of fork\n")
commit()
left_r_sha = base_revision()
left_f_sha = sha1("testfile")
check(left_r_sha ~= right_r_sha)
check(left_f_sha ~= right_f_sha)

check(mtn("--branch=testbranch", "heads"), 0, true, false)
check(not qgrep("empty", "stdout"))

probe_node("testfile", root_r_sha, root_f_sha)

writefile("testfile", "right version of fork\n")
check(mtn("add", "testfile"), 0, false, false)
commit()
right_r_sha = base_revision()
right_f_sha = sha1("testfile")
check(right_r_sha ~= root_r_sha)
check(right_f_sha ~= root_f_sha)
check(right_r_sha ~= left_r_sha)
check(right_f_sha ~= left_f_sha)

-- fork committed ok. now check to make sure
-- all 3 nodes are reconstructable

probe_node("testfile", root_r_sha, root_f_sha)
probe_node("testfile", left_r_sha, left_f_sha)
probe_node("testfile", right_r_sha, right_f_sha)

