
mtn_setup()

writefile("a")

check(mtn("add", "a"), 0, false, false)
commit()

root_r_sha = base_revision()
root_f_sha = sha1("a")

mkdir("b")
writefile("b/c")

check(mtn("add", "b"), 0, false, false)
commit()

probe_node("a", root_r_sha, root_f_sha)

remove("b")
writefile("d")

check(mtn("add", "d"), 0, false, false)
commit()

check(mtn("merge"), 0, false, false)
check(mtn("update"), 0, false, false)
