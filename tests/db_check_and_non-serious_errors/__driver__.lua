
mtn_setup()

-- Make sure that db check detects minor problems, but doesn't complain
-- about them too loudly (and doesn't exit with error status).

writefile("fileX", "blah blah")
writefile("fileY", "stuff stuff")

addfile("testfile", "more stuff")
commit()
rev = base_revision()

check(mtn("cert", rev, "author", "extra_author"), 0, false, false)

-- if we drop the file, we'll have a roster that doesn't
-- reference its own revision. 
-- we can then remove the revision to end up with a clean unreferenced roster.
check(mtn("drop", "--bookkeep-only", "testfile"), 0, false, false)
check(mtn("commit", "-m", "goingaway"), 0, false, false)
del_rev = base_revision()
for a,b in pairs({revisions = "id", revision_certs = "id", revision_ancestry = "child"}) do
  local str = string.format("delete from %s where %s = '%s'", a, b, del_rev)
  check(mtn("db", "execute", str), 0, false, false)
end

-- and also a few unused files shall float about
check(mtn("fload"), 0, false, false, {"fileX"})
check(mtn("fload"), 0, false, false, {"fileY"})

check(mtn("db", "check"), 0, false, true)

check(qgrep('problems detected: 4', "stderr"))
check(qgrep('0 serious', "stderr"))
check(qgrep('minor problems detected', "stderr"))
