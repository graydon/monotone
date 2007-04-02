
mtn_setup()

-- If a file is renamed from "testfile" to "otherfile" and has changes,
-- then 'mtn diff' should display:
--   --- testfile
--   +++ otherfile

addfile("testfile", "blah blah")
commit()

writefile("testfile", "stuff stuff")
check(mtn("rename", "testfile", "otherfile"), 0, false, false)

check(exists("otherfile"))

check(mtn("diff"), 0, true, false)

check(qgrep("--- testfile", "stdout"))
check(qgrep("\\+\\+\\+ otherfile", "stdout"))
