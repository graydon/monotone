
mtn_setup()

-- If we have a changeset representing a reversion, like
--    changeset_1:   patch "a" [id1] -> [id2]
--    changeset_2:   patch "a" [id2] -> [id1]
-- then concatenating these revisions should _not_ create a changeset
-- like
--    changeset_c:   patch "a" [id1] -> [id1]

revs = {}
files = {}

addfile("testfile", "version 1")
addfile("start_file", "start file")
commit()
revs.base = base_revision()
files.base = sha1("testfile")
files.base_other = sha1("start_file")

writefile("testfile", "version 2")
commit()

writefile("testfile", "version 1")
addfile("end_file", "end file")
check(mtn("drop", "--bookkeep-only", "start_file"), 0, true, false)
commit()
revs.new = base_revision()
files.new = sha1("testfile")
files.new_other = sha1("end_file")

check(files.base == files.new)

check(mtn("diff", "--revision", revs.base,
                  "--revision", revs.new),
      0, true, false)
check(qgrep(files.new_other, "stdout"))
check(not qgrep(files.base, "stdout"))
check(mtn("diff", "--revision", revs.new,
                  "--revision", revs.base),
      0, true, false)
check(qgrep(files.base_other, "stdout"))
check(not qgrep(files.base, "stdout"))
