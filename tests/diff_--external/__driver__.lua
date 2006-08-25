
mtn_setup()

addfile("testfile", "foo bar")
commit()
old = sha1("testfile")
copy("testfile", "old_testfile")

writefile("testfile", "stuff stuff")
new = sha1("testfile")

check(get("testhook.lua"))

check(mtn("--rcfile=testhook.lua", "diff", "--external"), 0, true, false)
canonicalize("stdout")
canonicalize("old_version")
canonicalize("new_version")
check(qgrep('file_path: testfile', "stdout"))
check(samefile("old_version", "old_testfile"))
check(samefile("new_version", "testfile"))
check(qgrep('diff_args is NIL', "stdout"))
check(qgrep("rev_old: "..old, "stdout"))
check(qgrep("rev_new: "..new, "stdout"))

check(mtn("--rcfile=testhook.lua", "diff", "--external", "--diff-args=-foobar"), 0, true, false)
canonicalize("stdout")
canonicalize("old_version")
canonicalize("new_version")
check(qgrep('file_path: testfile', "stdout"))
check(samefile("old_version", "old_testfile"))
check(samefile("new_version", "testfile"))
check(qgrep('diff_args: -foobar', "stdout"))
check(qgrep("rev_old: "..old, "stdout"))
check(qgrep("rev_new: "..new, "stdout"))

check(mtn("--rcfile=testhook.lua", "diff", "--external", "--diff-args", ""), 0, true, false)
canonicalize("stdout")
canonicalize("old_version")
canonicalize("new_version")
check(qgrep('file_path: testfile', "stdout"))
check(samefile("old_version", "old_testfile"))
check(samefile("new_version", "testfile"))
check(qgrep('^diff_args: $', "stdout"))
check(qgrep("rev_old: "..old, "stdout"))
check(qgrep("rev_new: "..new, "stdout"))

-- Make sure that --diff-args without --external is an error
check(mtn("diff", "--diff-args=foo"), 1, false, false)
