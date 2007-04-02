
mtn_setup()

-- This test is a bug report

-- 1. Alice starts a project and creates some files
-- 2. Bob checks out the project and gets the files
-- 3. Bob creates a file bar, but forgets to add it
-- 4. Alice renames one of her files to bar
-- 5. Bob updates
--
-- the update fails after moving renamed files to _MTN/tmp/N
-- where N is the file's tid.
--
-- Bob is left with a bunch of missing files.

mkdir("alice")

-- Alice starts a projectand creates foo
check(mtn("--branch=testbranch", "setup", "alice"), 0, false, false)
for i = 1,10 do
  writefile("alice/file."..i, "file "..i)
  check(indir("alice", mtn("add", "file."..i)), 0, false, false)
end
check(indir("alice", mtn("commit", "-m", 'alice adds files')), 0, false, false)

-- Bob checks out project, gets files and creates file bar
check(mtn("--branch=testbranch", "checkout", "bob"), 0, false, false)
writefile("bob/bar", "bob's bar")

-- Alice renames some files
check(indir("alice", mtn("rename", "--bookkeep-only", "file.3", "bar")), 0, false, false)
check(indir("alice", mtn("rename", "--bookkeep-only", "file.4", "bar.4")), 0, false, false)
check(indir("alice", mtn("rename", "--bookkeep-only", "file.5", "bar.5")), 0, false, false)
rename("alice/file.3", "alice/bar")
rename("alice/file.4", "alice/bar.4")
rename("alice/file.5", "alice/bar.5")
check(indir("alice", mtn("commit", "-m", 'alice renames files')), 0, false, false)

-- Bob updates but bar is in the way
xfail_if(true, indir("bob", mtn("update")), 0, false, false)


-- non-renamed files remain

check(exists("bob/file.1"))
check(exists("bob/file.2"))
for i in 6,10 do
  check(exists("bob/file."..i))
end

-- renamed files are gone

-- check(test -e bob/file.3, 1, true)
-- check(test -e bob/file.4, 1, true)
-- check(test -e bob/file.5, 1, true)

-- original bar still exists

-- check(exists("bob/bar"))

-- other renames are also gone

-- check(test -e bob/bar.4, 1, true)
-- check(test -e bob/bar.5, 1, true)
