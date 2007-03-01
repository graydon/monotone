
mtn_setup()

-- This one's kind of complicated.
-- 
--    base               - contains directory foo
--     |  \
--     |   \
--     |    rename_dir   - rename_dir foo bar
--     |          |
--     added      |      - add_file foo/a
--     |    \     |
--     |     \    |
--     |      \   |
--     |       merge 1
--     |           |
--     rename_file |     - rename_file foo/a bar/a
--             \   |
--              \  |
--               test
--                 
-- we want to make sure that both merges happen without tree conflicts
-- being triggered

revs = {}

mkdir("foo")
addfile("foo/bystander", "data data")
commit()
revs.base = base_revision()

check(mtn("rename", "--bookkeep-only", "foo/", "bar/"), 0, false, false)
rename("foo", "bar")
commit()
revs.rename_dir = base_revision()

revert_to(revs.base)

addfile("foo/a", "more data")
commit()
revs.added = base_revision()

check(mtn("merge"), 0, false, false)

rename("foo/a", "foo/b")
check(mtn("rename", "--bookkeep-only", "foo/a", "foo/b"), 0, false, false)
commit()
revs.rename_file = base_revision()

check(mtn("merge"), 0, false, false)

check(mtn("checkout", "--revision", revs.base, "test_dir"), 0, false, false)
check(indir("test_dir", mtn("update", "--branch=testbranch")), 0, false, false)
revs.test = indir("test_dir", {base_revision})[1]()

for _,x in pairs{"base", "rename_dir", "added", "rename_file"} do
  check(revs.test ~= revs[x])
end

check(not exists("test_dir/foo/bystander"))
check(exists("test_dir/bar/bystander"))
check(not exists("test_dir/foo/a"))
check(not exists("test_dir/bar/a"))
check(not exists("test_dir/foo/b"))
check(exists("test_dir/bar/b"))
