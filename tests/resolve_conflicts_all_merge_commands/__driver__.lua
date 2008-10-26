-- Demonstrate that all non-workspace merge commands support --resolve-conflicts.
--
-- The merge commands are defined in cmd_merging.cc; search for CMD.
-- They are: merge, propagate, explicit_merge, merge_into_dir.
--
-- propagate is implemented by calling merge_into_dir,
-- so we don't need to test the latter explicitly.

-- We don't explicitly test --resolve-conflicts-file, because that is
-- handled by the same code, and other tests show it works.
--
-- See other resolve_conflicts_* tests for individual conflict
-- resolutions.

-- The test structure is borrowed from ../conflict_messages/__driver__.lua

mtn_setup()

function setup(branch)
    remove("_MTN")
    remove("foo")
    remove("bar")
    remove("baz")
    remove(branch)
    check(mtn("setup", ".", "--branch", branch), 0, false, false)
end

function merged_revision()
  local workrev = readfile("stderr")
  local extract = string.gsub(workrev, "^.*mtn: %[merged%] (%x*).*$", "%1")
  if extract == workrev then
    err("failed to extract merged revision from stderr")
  end
  return extract
end

branch = "content-attached"
setup(branch)

addfile("foo", branch .. "-foo")
commit(branch)
base = base_revision()

writefile("foo", branch .. "-foo first revision")
commit(branch)
first = base_revision()

revert_to(base)

writefile("foo", branch .. "-foo second revision")

commit(branch .. "-propagate")
second = base_revision()

check(mtn("conflicts", "store", "h:" .. branch , "h:" .. branch .. "-propagate"), 0, nil, true)
check(mtn("conflicts", "resolve_first", "user", "foo"), 0, nil, nil)

check(mtn("propagate", branch , branch .. "-propagate", "--resolve-conflicts"), 0, nil, true)
merged = merged_revision()
check(mtn("db", "kill_rev_locally", merged), 0, nil, true)

check(mtn("explicit_merge", first, second, branch,"--resolve-conflicts"), 0, nil, true)
merged = merged_revision()
check(mtn("db", "kill_rev_locally", merged), 0, nil, true)

-- create a second head on 'branch'
writefile("foo", branch .. "-foo third revision")
commit(branch)
check(mtn("conflicts", "store"), 0, nil, true)
check(mtn("conflicts", "resolve_first", "user", "foo"), 0, nil, nil)
check(mtn("merge", "--branch", branch, "--resolve-conflicts"), 0, nil, true)
merged = merged_revision()
check(mtn("db", "kill_rev_locally", merged), 0, nil, true)

-- end of file
