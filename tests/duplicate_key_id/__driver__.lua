mtn_setup()

remove("test.db")
check(mtn("db", "init"), 0, false, false)

check(get("bad_test_key", "stdin"))
check(mtn("read"), 0, false, false, true)

addfile("testfile", "version 0 of test file")
check(mtn("commit", "-m", "try to commit with bad key in DB"), 1, false, true)
check(qgrep("The key 'tester@test.net' stored in your database", "stderr"))

check(mtn("ls", "keys"), 0, false, true)
check(qgrep("Mismatched Key: tester@test.net", "stderr"))
