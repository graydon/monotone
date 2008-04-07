
mtn_setup()

-- the following commands accept file arguments and --exclude and --depth
-- options used to define a restriction on the files that will be processed:
-- 
-- ls unknown
-- ls ignored
-- ls missing
-- ls known
-- status
-- diff
-- commit
-- revert
--

-- this test ensures that these commands operate on the same set of files given
-- the same restriction specification.  maintaining consistency across these
-- commands allows for destructive commands (commit and revert) to be "tested"
-- first with non-destructive commands (ls unknown/ignored/missing/known, status,
-- diff)

-- macros for running and verifying tests

function patch_files(dat)
writefile("file1", "file1 "..dat)
writefile("file2", "file2 "..dat)
writefile("foo/foo1", "foo1 "..dat)
writefile("foo/foo2", "foo2 "..dat)
writefile("foo/bar/bar1", "bar1 "..dat)
writefile("foo/bar/bar2", "bar2 "..dat)
end

allfiles = {"file1", "file2", "foo/foo1", "foo/foo2", "foo/bar/bar1", "foo/bar/bar2"}

function lookfor(file, want, nowant)
  for _,x in ipairs(want) do check(qgrep(x, file)) end
  for _,x in ipairs(nowant) do check(not qgrep(x, file)) end
end

function chk(cmd, where, want, nowant)
  check(mtn(unpack(cmd)), 0, true, true)
  lookfor(where, want, nowant)
end

-- test restrictions and associated lists of included/excluded files

data = {}

data.root = {}
data.root.args = {"."}
data.root.included = allfiles
data.root.excluded = {}

data.include = {}
data.include.args = {"file1", "foo/foo1", "foo/bar/bar1"}
data.include.included = {"file1", "foo/foo1", "foo/bar/bar1"}
data.include.excluded = {"file2", "foo/foo2", "foo/bar/bar2"}

data.exclude = {}
data.exclude.args = {".", "--exclude", "file1", "--exclude", "foo/foo1", "--exclude", "foo/bar/bar1"}
data.exclude.included = {"file2", "foo/foo2", "foo/bar/bar2"}
data.exclude.excluded = {"file1", "foo/foo1", "foo/bar/bar1"}

data.both = {}
data.both.args = {"foo", "--exclude", "foo/foo1", "--exclude", "foo/bar/bar1"}
data.both.included = {"foo/foo2", "foo/bar/bar2"}
data.both.excluded = {"file1", "file2", "foo/foo1", "foo/bar/bar1"}

data.depth = {}
data.depth.args = {".", "--depth", "2"}
data.depth.included = {"file1", "file2", "foo/foo1", "foo/foo2"}
data.depth.excluded = {"foo/bar/bar1", "foo/bar/bar2"}

function checkall(cmd, where, precmd)
  local what = ""
  for _,x in ipairs(cmd) do what = what..tostring(x).." " end
  for name,t in pairs(data) do
    L("Now checking: ", what, name, "\n")
    local args = {}
    for _,x in ipairs(cmd) do table.insert(args,x) end
    for _,x in ipairs(t.args) do table.insert(args,x) end
    if precmd ~= nil then precmd(name) end
    chk(args, where, t.included, t.excluded)
  end
end

-- setup workspace
mkdir("foo")
mkdir("foo/bar")
patch_files("initial addition of files")
check(mtn("add", "-R", "file1", "file2", "foo"), 0, false, false)
commit()

-- check that ls unknown/ignored/missing/known, status, diff, revert and commit
-- all agree on what is included/excluded by various restrictions

-- ls unknown 
-- dropped files are valid for restriction but are unknown in the post-state
check(mtn("drop", "--bookkeep-only", unpack(allfiles)), 0, false, false)
checkall({"ls", "unknown"}, "stdout")
check(mtn("revert", "."), 0, false, false)

-- ls ignored
check(get("ignore.lua"))
-- only unknown files are considered by ls ignored
check(mtn("drop", "--bookkeep-only", unpack(allfiles)), 0, false, false)
checkall({"ls", "ignored", "--rcfile=ignore.lua"}, "stdout")
check(mtn("revert", "."), 0, false, false)

-- ls missing
for _,x in pairs(allfiles) do remove(x) end
checkall({"ls", "missing"}, "stdout")
check(mtn("revert", "."), 0, false, false)


patch_files("changes for testing ls known, status, diff")

-- ls known
checkall({"ls", "known"}, "stdout")

-- status
checkall({"status"}, "stdout")

-- diff
checkall({"diff"}, "stdout")

-- revert
checkall({"revert"}, "stderr", function(x) patch_files("revert "..x) end)

-- commit
for name,t in pairs(data) do
  local args = {"commit", "-m", name}
  for _,x in ipairs(t.args) do table.insert(args,x) end
  
  local old = base_revision()
  patch_files("commit "..name)
  check(mtn(unpack(args)), 0, false, false)
  local new = base_revision()
  
  chk({"status"}, "stdout", t.excluded, t.included)
  chk({"diff", "-r", old, "-r", new}, "stdout", t.included, t.excluded)
end
