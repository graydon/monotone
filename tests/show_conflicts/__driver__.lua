
mtn_setup()

addfile("foo", "file foo\n")
addfile("bar", "file bar\n")
commit("branch")
base = base_revision()

check(mtn("mv", "foo", "baz"), 0, false, false)
append("bar", "xxx\n")
commit("branch")
left = base_revision()

check(mtn("update", "-r", base), 0, false, false)
check(mtn("mv", "foo", "quux"), 0, false, false)
append("bar", "yyy\n")
commit("branch")
right = base_revision()

check(mtn("show_conflicts", left, right), 0, false, true)
rename("stderr", "conflicts")

check(qgrep("There are 1 divergent_name_conflicts", "conflicts"))
check(qgrep("There are 1 file_content_conflicts", "conflicts"))
check(qgrep("There are 0 node_attr_conflicts", "conflicts"))
check(qgrep("There are 0 orphaned_node_conflicts", "conflicts"))
check(qgrep("There are 0 convergent_name_conflicts", "conflicts"))
check(qgrep("There are 0 directory_loop_conflicts", "conflicts"))
