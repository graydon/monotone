
mtn_setup()

addfile("testfile", "foo")
commit()
rev = base_revision()

copy("test.db", "test-old.db")
writefile("testfile", "blah")
commit()

testURI="file:" .. test.root .. "/test.db"

-- We use RAW_MTN because it used to be that passing --db= (as
-- MTN does) would hide a bug in this functionality...

-- all of these inherit options settings from the current _MTN dir
-- unless they override them on the command line

check(nodb_mtn("--branch=testbranch", "clone", testURI, "test_dir1"), 0, false, false)
check(nodb_mtn("--branch=testbranch", "clone", testURI, "--revision", rev, "test_dir2"), 0, false, false)
check(nodb_mtn("--db=" .. test.root .. "/test-old.db", "--branch=testbranch", "clone", testURI, "test_dir3"), 0, false, false)
check(nodb_mtn("--db=" .. test.root .. "/test-old.db", "--branch=testbranch", "clone", testURI, "--revision", rev, "test_dir4"), 0, false, false)

-- checkout fails if the specified revision is not a member of the specified branch
check(nodb_mtn("--branch=foobar", "clone", testURI, "--revision", rev, "test_dir5"), 1, false, false)
check(mtn("cert", rev, "branch", "foobar"), 0, false, false)
check(nodb_mtn("--branch=foobar", "clone", testURI, "--revision", rev, "test_dir6"), 0, false, false)


for i = 1,2 do
  local dir = "test_dir"..i
  L("dir = ", dir, "\n")
  check(exists(dir.."/_MTN/options"))
  check(qgrep(dir.."/_MTN/mtn.db", dir.."/_MTN/options"))
  check(qgrep("testbranch", dir.."/_MTN/options"))
end

for i = 3,4 do
  local dir = "test_dir"..i
  L("dir = ", dir, "\n")
  check(exists(dir.."/_MTN/options"))
  check(qgrep("test-old.db", dir.."/_MTN/options"))
  check(qgrep("testbranch", dir.."/_MTN/options"))
end

check(qgrep("foobar", "test_dir6/_MTN/options"))
