
mtn_setup()

check(get("root", "testfile"))
check(mtn("add", "testfile"), 0, false, false)
commit()
root_r_sha = base_revision()
root_f_sha = sha1("testfile")

check(get("good", "testfile"))
commit()
left_good_r_sha = base_revision()
left_good_f_sha = sha1("testfile")
check(left_good_r_sha ~= root_r_sha)
check(left_good_f_sha ~= root_f_sha)

check(get("bad", "testfile"))
commit()
left_bad_r_sha = base_revision()
left_bad_f_sha = sha1("testfile")
check(left_bad_r_sha ~= left_good_r_sha)
check(left_bad_f_sha ~= left_good_f_sha)

probe_node("testfile", root_r_sha, root_f_sha)

check(get("work", "testfile"))
check(mtn("testresult", root_r_sha, "1"), 0, false, false)
check(mtn("testresult", left_good_r_sha, "1"), 0, false, false)
check(mtn("testresult", left_bad_r_sha, "0"), 0, false, false)
check(mtn("update"), 0, false, false)

-- files should now be merged

check(get("final", "probe"))
check(samefile("testfile", "probe"))
