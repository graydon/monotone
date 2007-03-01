
mtn_setup()

-- patch existing file
-- add new file (with patch)
-- rename existing file
-- rename and patch existing file
-- drop existing file

-- again with --brief

addfile("from", "from")
addfile("from_patched", "from_patched")
addfile("patched", "patched")
addfile("dropped", "dropped")

commit()

addfile("added", "added")

writefile("from_patched", "from_patched \npatched")
writefile("patched", "patched \npatched")

check(mtn("drop", "--bookkeep-only", "dropped"), 0, false, false)

rename("from", "to")
rename("from_patched", "to_patched")

check(mtn("rename", "--bookkeep-only", "from", "to"), 0, false, false)
check(mtn("rename", "--bookkeep-only", "from_patched", "to_patched"), 0, false, false)

check(mtn("status"), 0, true, false)
check(qgrep('^  dropped  dropped', "stdout"))
check(qgrep('^  renamed  from', "stdout"))
check(qgrep('^       to  to', "stdout"))
check(qgrep('^  renamed  from_patched', "stdout"))
check(qgrep('^       to  to_patched', "stdout"))
check(qgrep('^  added    added', "stdout"))
check(qgrep('^  patched  patched', "stdout"))
check(qgrep('^  patched  to_patched', "stdout"))

-- restricted

check(mtn("status", "from", "from_patched"), 0, true, false)
check(not qgrep('^  delete "dropped"', "stdout"))
check(qgrep('^  renamed  from', "stdout"))
check(qgrep('^       to  to', "stdout"))
check(qgrep('^  renamed  from_patched', "stdout"))
check(qgrep('^       to  to_patched', "stdout"))
check(not qgrep('^  add_file  added', "stdout"))
check(not qgrep('^  patched  patched', "stdout"))
check(qgrep('^  patched  to_patched', "stdout"))
