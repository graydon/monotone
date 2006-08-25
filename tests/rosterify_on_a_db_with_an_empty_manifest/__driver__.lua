
mtn_setup()

-- This is a db containing two revisions. The first adds a single file (afile).
-- The second revision deletes that file, leaving an empty manifest.

remove("test.db")

check(get("test.db.dumped", "stdin"))
check(mtn("db", "load"), 0, false, false, true)
check(mtn("db", "migrate"), 0, false, false)

check(mtn("db", "rosterify"), 0, false, false)

check(mtn("automate", "select", "h:testbranch"), 0, true)
rev = trim(readfile("stdout"))

check(get("revision_good"))
check(mtn("automate", "get_revision", rev), 0, {"revision_good"})

check(get("manifest_good"))
check(mtn("automate", "get_manifest_of", rev), 0, {"manifest_good"})
