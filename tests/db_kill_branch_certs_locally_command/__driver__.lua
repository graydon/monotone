
mtn_setup()

-- This tests the db kill_branch_certs_locally command

-- Prepare a db with a couple of branches
addfile("foo", "file named foo")
commit("good")
rev = base_revision()
check(mtn("cert", rev, "branch", "bad"), 0, false, false)
check(mtn("ls", "branches"), 0, true, false)
check(qgrep("good", "stdout"))
check(qgrep("bad", "stdout"))

-- Now we delete the branch, and make sure it's gone
check(mtn("db", "kill_branch_certs_locally", "bad"), 0, false, false)
check(mtn("ls", "branches"), 0, true, false)
check(qgrep("good", "stdout"))
check(not qgrep("bad", "stdout"))

-- And lets make sure our database is still OK
check(mtn("db", "check"), 0, false, false)
