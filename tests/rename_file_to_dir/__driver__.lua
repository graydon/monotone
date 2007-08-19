
mtn_setup()

-- this used to be a bug report, but the semantics have now been changed such
-- that if dest is a dir and exists (doesn't have to be versioned), then
-- the result of mtn mv file dir is dir/file as it would be with a posix mv(1)
-- the side effect is that dir will be added to the roster as if 'by magic'

addfile("file", "file")
commit()

mkdir("dir")
check(mtn("rename", "--bookkeep-only", "file", "dir"), 0, false, false)
-- status and diff will now complain about missing files
check(mtn("status"), 1, false, false)
check(mtn("diff"), 1, false, false)
