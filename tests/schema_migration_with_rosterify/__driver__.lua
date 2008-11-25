
mtn_setup()

-- This test takes databases in all the old pre-roster formats, and
-- checks that migrating them forward then rosterifying them works.

-- We actually don't test against old-format databases directly,
-- because some old-format databases can't be read at all by a modern
-- monotone -- you have to do a dump/load first.  So instead we store
-- pre-dumped old-format databases.  So technically we're not checking
-- that 'db migrate' can handle things, we're just checking that 'dump
-- | load; db migrate' can handle things.  But that should be good
-- enough.

--------------------------------------------------------------------------------------------------------------------------------------------
---- Do not touch this code; you'll have to regenerate all the test
---- databases if you do!
--------------------------------------------------------------------------------------------------------------------------------------------

writefile("blah_blah.txt", "blah-blah")

-- We don't want the standard db, we want full control ourselves
remove("test.db")
remove("keys")
check(mtn("db", "init"))

-- Put some random keys in, with and without corresponding private keys
check(get("migrate_keys"))
check(mtn("read"), 0, false, false, {"migrate_keys"})

addfile("testfile1", "f1v1\n")
addfile("testfile2", "f2v1\n")
check(mtn("commit", "--branch=testbranch1", "--message-file=blah_blah.txt"), 0, false, false)
rev=base_revision()

check(mtn("cert", rev, "somekey", "somevalue"), 0, false, false)

writefile("testfile1", "f1v2\n")
addfile("testfile3", "f3v1\n")
check(mtn("commit", "--branch=testbranch2", "--message-file=blah_blah.txt"), 0, false, false)

revert_to(rev)
remove("testfile3")

writefile("testfile2", "f2v2\n")
addfile("testfile4", "f4v1\n")
check(mtn("commit", "--branch=testbranch1", "--message-file=blah_blah.txt"), 0, false, false)

check(get("old_revs_propagate_log"))
check(mtn("propagate", "testbranch2", "testbranch1",
          "--message-file=old_revs_propagate_log", "--no-prefix"), 0, false, false)
check(mtn("update"), 0, false, false)

check(mtn("drop", "--bookkeep-only", "testfile1"), 0, false, false)
writefile("testfile4", "f4v2\n")
check(mtn("commit", "--branch=testbranch3", "--message-file=blah_blah.txt"), 0, false, false)

-- Exception to this code being untouchable:
-- This line may have to be modified at a later date; this won't cause
-- any problem, as long as it's replaced by code with the same effect.
check(mtn("db", "execute", "DELETE FROM revision_certs WHERE name = 'date'"), 0, false, false)

copy("test.db", "latest.mtn")

if debugging then
  check(mtn("--db=latest.mtn", "db", "dump"), 0, true, false)
  rename("stdout", "latest.mtn.dumped")
  check(mtn("--db=latest.mtn", "db", "version"), 0, true, false)
  local ver = string.gsub(readfile("stdout"), "^.*: (.*)%s$", "%1")
  rename("latest.mtn.dumped", ver..".mtn.dumped")
end

--------------------------------------------------------------------------------------------------------------------------------------------
---- End untouchable code
--------------------------------------------------------------------------------------------------------------------------------------------

function check_migrate_from(id)
  -- id.dumped is a 'db dump' of a db with schema "id"
  get(id..".mtn.dumped", "stdin")
  check(mtn("--db="..id..".mtn", "db", "load"), 0, false, false, true)
  -- check that the version's correct
  check(mtn("--db="..id..".mtn", "db", "version"), 0, true, false)
  check(qgrep(id, "stdout"))
  -- migrate it
  check(mtn("--db="..id..".mtn", "db", "migrate"), 0, false, false)
  check(mtn("--db="..id..".mtn", "db", "rosterify"), 0, false, false)
  check_same_db_contents(id..".mtn", "latest.mtn")
end


check_migrate_from("c1e86588e11ad07fa53e5d294edc043ce1d4005a")
check_migrate_from("40369a7bda66463c5785d160819ab6398b9d44f4")
check_migrate_from("e372b508bea9b991816d1c74680f7ae10d2a6d94")
check_migrate_from("1509fd75019aebef5ac3da3a5edf1312393b70e9")
check_migrate_from("bd86f9a90b5d552f0be1fa9aee847ea0f317778b")
