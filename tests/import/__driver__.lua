mtn_setup()

mkdir("importdir")

------------------------------------------------------------------------------
-- First attempt, import something completely fresh.
writefile("importdir/importmefirst", "version 0 of first test file\n")
writefile("importdir/.mtn-ignore", "CVS\n")

check(mtn("import", "importdir",
	  "--message", "Import one, fresh start",
	  "--branch", "importbranch"), 0, false, false)

check(mtn("checkout", "exportdir1", "--branch", "importbranch"),
      0, false, false)

check(samefile("importdir/importmefirst", "exportdir1/importmefirst"))

------------------------------------------------------------------------------
-- Second attempt, import something with a changed file.
writefile("importdir/importmefirst", "version 1 of first test file\n")
writefile("importdir/importmeignored", "version 0 of first ignored file\n")

check(mtn("import", "importdir",
	  "--no-respect-ignore",
	  "--exclude", "importmeignored",
	  "--message", "Import two, a changed file",
	  "--branch", "importbranch"), 0, false, false)

remove("importdir/importmeignored")

check(mtn("checkout", "exportdir2", "--branch", "importbranch"),
      0, false, false)

check(not exists("exportdir2/importmeignored"))
check(samefile("importdir/importmefirst", "exportdir2/importmefirst"))

------------------------------------------------------------------------------
-- Third attempt, import something with an added file.
writefile("importdir/importmesecond", "version 0 of second test file\n")
writefile("message", "Import three, an added file")

check(mtn("import", "importdir",
	  "--message-file", "message",
	  "--branch", "importbranch"), 0, false, false)

check(mtn("checkout", "exportdir3", "--branch", "importbranch"),
      0, false, false)
check(mtn("automate", "heads", "importbranch"), 0, true, false)
rsha1 = trim(readfile("stdout"))

check(samefile("importdir/importmefirst", "exportdir3/importmefirst"))
check(samefile("importdir/importmesecond", "exportdir3/importmesecond"))

------------------------------------------------------------------------------
-- Fourth attempt, import something with a changed and a dropped file.
remove("importdir/importmefirst")
writefile("importdir/importmesecond", "version 1 of second test file\n")

check(mtn("import", "importdir",
	  "--message", "Import four, a changed and a dropped file",
	  "--branch", "importbranch"), 0, false, false)

check(mtn("checkout", "exportdir4", "--branch", "importbranch"),
      0, false, false)

check(not exists("exportdir4/importmefirst"))
check(samefile("importdir/importmesecond", "exportdir4/importmesecond"))

------------------------------------------------------------------------------
-- Fifth attempt, this time adding a third file and importing relative to
-- an earlier revision
writefile("importdir/importmethird", "version 0 of third test file\n")

check(mtn("import", "importdir",
	  "--message", "Import five, an added file and relative to a specific revision",
	  "--revision", rsha1), 0, false, true)
rsha2 = string.gsub(readfile("stderr"),
		    "^.*committed revision ([0-9a-f]+).*$", "%1")

check(mtn("checkout", "exportdir5", "--revision", rsha2),
      0, false, false)

check(not exists("exportdir5/importmefirst"))
check(samefile("importdir/importmesecond", "exportdir5/importmesecond"))
check(samefile("importdir/importmethird", "exportdir5/importmethird"))

------------------------------------------------------------------------------
-- Sixth attempt, dropping a file and.
-- Trying again against the head of the branch.  Since there's a fork,
-- this attempt is expected to FAIL.
remove("importdir/importmesecond")

check(mtn("import", "importdir",
	  "--message", "Import six, an dropped file and relative to the heads",
	  "--branch", "importbranch"), 1, false, false)

-- Let's do a merge and try again.
check(mtn("merge", "--branch", "importbranch"), 0, false, false)

check(mtn("import", "importdir",
	  "--message", "Import six, an dropped file and relative to the heads",
	  "--branch", "importbranch"), 0, false, false)

check(mtn("checkout", "exportdir6", "--branch", "importbranch"),
      0, false, false)

check(not exists("exportdir6/importmefirst"))
check(not exists("exportdir6/importmesecond"))
check(samefile("importdir/importmethird", "exportdir6/importmethird"))

------------------------------------------------------------------------------
-- Seventh attempt, importing from one of the export checkouts.
-- This attempt is expected to FAIL, because import should refuse to
-- import from a workspace.
check(mtn("import", "exportdir2",
	  "--message", "Import seven, trying to import a workspace",
	  "--branch", "importbranch"), 1, false, false)

------------------------------------------------------------------------------
-- Eight attempt, this time just doing a dry run.
remove("importdir/importmethird")
check(mtn("import", "importdir",
	  "--dry-run",
	  "--message", "Import eight, dry run so shouldn't commit",
	  "--branch", "importbranch"), 0, false, true)
check(not qgrep("committed revision ", "stderr"))

------------------------------------------------------------------------------
-- Ninth attempt, importing from one of the export checkouts.
-- This attempt is expected to FAIL, because we gave it a non-existing key.
-- However, we want to check that such an error didn't leave a _MTN behind.
check(mtn("import", "importdir",
	  "--message", "Import nine, trying to import a workspace",
	  "--branch", "importbranch",
	  "--key", "bozo@bozoland.com"), 1, false, false)
check(not exists("importdir/_MTN"))

------------------------------------------------------------------------------
-- Tenth attempt, importing with an added subdirectory
mkdir("importdir/subdir10")
writefile("importdir/subdir10/importmesubdir", "version 0 of subdir file\n")

check(mtn("import", "importdir",
	  "--message", "Import ten, trying to import a workspace subdirectory",
	  "--branch", "importbranch"), 0, false, false)

check(mtn("checkout", "exportdir10", "--branch", "importbranch"),
      0, false, false)

check(exists("exportdir10/subdir10/importmesubdir"))

------------------------------------------------------------------------------
-- Eleventh attempt, checking that ignorable files aren't normally imported
mkdir("importdir/subdir11")
writefile("importdir/fake_test_hooks.lua", "version 0 of ignored file\n")
writefile("importdir/subdir11/fake_test_hooks.lua", "version 0 of subdir fake ignored file\n")

check(mtn("import", "importdir",
	  "--message", "Import eleven, trying to import a normally ignored file",
	  "--branch", "importbranch"), 0, false, false)

check(mtn("checkout", "exportdir11", "--branch", "importbranch"),
      0, false, false)

check(not exists("exportdir11/fake_test_hooks.lua"))
check(not exists("exportdir11/subdir11/fake_test_hooks.lua"))

------------------------------------------------------------------------------
-- twelth attempt, now trying again, without ignoring.
mkdir("importdir/subdir12")
writefile("importdir/fake_test_hooks.lua", "version 0 of ignored file\n")
writefile("importdir/subdir12/fake_test_hooks.lua", "version 0 of subdir fake ignored file\n")

check(mtn("import", "importdir", "--no-respect-ignore",
	  "--message", "Import twelve, trying to import a normally ignored file",
	  "--branch", "importbranch"), 0, false, false)

check(mtn("checkout", "exportdir12", "--branch", "importbranch"),
      0, false, false)

check(exists("exportdir12/fake_test_hooks.lua"))
check(exists("exportdir12/subdir12/fake_test_hooks.lua"))
