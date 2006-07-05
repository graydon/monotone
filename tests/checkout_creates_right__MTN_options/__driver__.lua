
mtn_setup()

addfile("testfile", "foo")
commit()
rev = base_revision()

-- We use RAW_MTN because it used to be that passing --db= (as
-- MTN does) would hide a bug in this functionality...

-- all of these inherit options settings from the current _MTN dir
-- unless they override them on the command line

check(raw_mtn("checkout", "test_dir1"), 0, false, false)
check(raw_mtn("--db=test.db", "checkout", "test_dir2"), 0, false, false)
check(raw_mtn("--db=test.db", "--branch=testbranch", "checkout", "test_dir3"), 0, false, false)
check(raw_mtn("--branch=testbranch", "checkout", "test_dir4"), 0, false, false)
check(raw_mtn("--db=test.db", "--branch=testbranch", "checkout", "--revision", rev, "test_dir5"), 0, false, false)
check(raw_mtn("--branch=testbranch", "checkout", "--revision", rev, "test_dir6"), 0, false, false)
check(raw_mtn("--db=test.db", "checkout", "--revision", rev, "test_dir7"), 0, false, false)
check(raw_mtn("checkout", "--revision", rev, "test_dir8"), 0, false, false)

-- checkout fails if the specified revision is not a member of the specified branch
check(raw_mtn("--branch=foobar", "checkout", "--revision", rev, "test_dir9"), 1, false, false)
check(mtn("cert", rev, "branch", "foobar"), 0, false, false)
check(raw_mtn("--branch=foobar", "checkout", "--revision", rev, "test_dir10"), 0, false, false)


for i = 1,8 do
  local dir = "test_dir"..i
  L("dir = ", dir, "\n")
  check(exists(dir.."/_MTN/options"))
  check(qgrep("test.db", dir.."/_MTN/options"))
  check(qgrep("testbranch", dir.."/_MTN/options"))
end

check(qgrep("foobar", "test_dir10/_MTN/options"))
