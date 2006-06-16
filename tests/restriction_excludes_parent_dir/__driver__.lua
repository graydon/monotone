
mtn_setup()

-- - restricting a new project excludes the root directory addition
-- - restricting to things below a newly added directory excludes the directory
-- - excluding added directories but including things they contain is bad
--
-- we should issue a nice error indicating that such a restriction must
-- be expanded to include the containing directories.
-- we should also provide some way to include only these directories and not
-- all of their contents. 
--
-- --depth=0 should probably mean "directory without contents" rather than
-- "directory and all immediate children"

addfile("file", "file")

-- exclude newly added root dir but include file below it
check(mtn("st", "file"), 1, false, false)

commit()

mkdir("foo")

addfile("foo/bar", "foobar")


-- exclude newly added foo dir but include bar below it
check(mtn("st", "foo/bar"), 1, false, false)
