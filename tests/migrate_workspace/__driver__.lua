-- General checks of the migrate_workspace command; see workspace_migration
-- for exhaustive checks of migration from each possible format version.

mtn_setup()

-- mtn migrate_workspace requires a workspace.
rename("_MTN","_MTN.not")
check(mtn("migrate_workspace"), 1, nil, true)
check(qgrep("workspace required but not found", "stderr"))
rename("_MTN.not","_MTN")

-- Given a workspace in the current format, migrate_workspace should
-- just print a message and exit successfully.
addfile("foo", "foo")
commit()
check(mtn("migrate_workspace"), 0, nil, true)
check(qgrep("workspace is in the current format", "stderr"))

-- Given a workspace in an absurdly high format, migrate_workspace
-- should fail, and so should ordinary operations on the workspace.
writefile("_MTN/format", "999999999\n")
check(mtn("status"), 1, nil, true)
check(qgrep("need a newer version", "stderr"))
check(qgrep("^999999999$", "_MTN/format"))
check(mtn("migrate_workspace"), 1, nil, true)
check(qgrep("need a newer version", "stderr"))
check(qgrep("^999999999$", "_MTN/format"))

-- We would test migration from format 0, but it is not possible to do
-- that migration short of destroying the workspace and starting over
-- (see comments in work_migration.cc).  So we just check that the
-- attempt fails (and leaves the workspace alone).

remove("_MTN/format")
rename("_MTN","MT")
check(mtn("status"), 1, nil, true)
check(qgrep("metadata is in format 0", "stderr"))
check(exists("MT"))
check(mtn("migrate_workspace"), 1, nil, true)
check(qgrep("not possible to migrate from workspace format 0", "stderr"))
check(exists("MT"))
