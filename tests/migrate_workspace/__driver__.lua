-- Test migration of workspace format.

-- This constant must be kept in sync with work_migration.cc.
current_workspace_format = "2"

-- If we ever change the format or content of _MTN/options we will
-- have to think of something else to do here.
mtn_setup()
copy("_MTN/options", "saved-options")

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

-- Migration from format 1.  First, test this process with a working
-- directory corresponding to an unmodified checkout of the database
-- we created above.

remove("MT")
gettree("format-1-unmodified")
copy("saved-options", "_MTN/options")
check(mtn("status"), 1, nil, true)
check(qgrep("must be migrated", "stderr"))

check(mtn("migrate_workspace"), 0, nil, nil)
check(qgrep("^"..current_workspace_format.."$", "_MTN/format"))

check(mtn("status"), 0, true, nil)
check(qgrep("^  no changes$", "stdout"))

-- Now test migration in a workspace with pending content changes.
-- This has no visible effect on a format-1 bookkeeping directory,
-- so we can use the same tree file to test it.

writefile("foo", "bar")
check(mtn("status"), 0, true, nil)
check(qgrep("^  patched foo$", "stdout"))

remove("_MTN")
gettree("format-1-unmodified")
copy("saved-options", "_MTN/options")

check(mtn("status"), 1, nil, true)
check(qgrep("must be migrated", "stderr"))

check(mtn("migrate_workspace"), 0, nil, nil)
check(qgrep("^"..current_workspace_format.."$", "_MTN/format"))

check(mtn("status"), 0, true, nil)
check(qgrep("^  patched foo$", "stdout"))

-- Now test migration in a workspace with a pending rename.
-- Format 1 represents this with a "work" file; format 2 with additional
-- data in the "revision" file.

check(mtn("revert", "foo"), 0, false, false)
check(mtn("mv", "-e", "foo", "bar"), 0, false, false)

check(mtn("status"), 0, true, nil)
check(qgrep("^  renamed foo$", "stdout"))
check(qgrep("^       to bar$", "stdout"))

remove("_MTN")
gettree("format-1-rename")
copy("saved-options", "_MTN/options")

check(mtn("status"), 1, nil, true)
check(qgrep("must be migrated", "stderr"))

check(mtn("migrate_workspace"), 0, nil, nil)
check(qgrep("^"..current_workspace_format.."$", "_MTN/format"))

check(mtn("status"), 0, true, nil)
check(qgrep("^  renamed foo$", "stdout"))
check(qgrep("^       to bar$", "stdout"))
