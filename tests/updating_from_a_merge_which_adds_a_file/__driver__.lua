
mtn_setup()

writefile("a")

check(cmd(mtn("add", "a")), 0, false, false)
commit()

root_r_sha = base_revision()
root_f_sha = sha1("a")

mkdir("b")
writefile("b/c")

check(cmd(mtn("add", "b")), 0, false, false)
commit()

probe_node("a", root_r_sha, root_f_sha)

remove_recursive("b")
writefile("d")

check(cmd(mtn("add", "d")), 0, false, false)
commit()

check(cmd(mtn("merge")), 0, false, false)
check(cmd(mtn("update")), 0, false, false)
