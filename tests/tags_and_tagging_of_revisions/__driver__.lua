
mtn_setup()

writefile("file1", "file 1")
writefile("file2", "file 2")
writefile("file3", "file 3")

-- make sure a tag of a nonexistent revision fails
check(mtn("tag", "af2f6c1f3b7892672357a1018124ee80c752475d", "foo"), 1, false, false)

revs = {}

-- make and tag revision 1

check(mtn("add", "file1"), 0, false, false)
commit()
revs[1] = base_revision()
check(mtn("tag", revs[1], "tag1"), 0, false, false)

-- make and tag revision 2

check(mtn("add", "file2"), 0, false, false)
commit()
revs[2] = base_revision()
check(mtn("tag", revs[2], "tag2"), 0, false, false)

-- make and tag revision 3

check(mtn("add", "file3"), 0, false, false)
commit()
revs[3] = base_revision()
check(mtn("tag", revs[3], "tag3"), 0, true, true)

-- check tags created above

check(mtn("ls", "tags"), 0, true, false)
check(qgrep("tag1", "stdout"))
check(qgrep("tag2", "stdout"))
check(qgrep("tag3", "stdout"))

-- make sure 'ls tags' output is sorted
if existsonpath("sort") then
  canonicalize("stdout")
  copy("stdout", "stdin")
  rename("stdout", "stdout-orig")
  check({"sort"}, 0, readfile("stdout-orig"), false, true)
end

for i,x in pairs({{true, false, false},
                  {true, true, false},
                  {true, true, true}}) do
  remove("_MTN")
  remove("file1")
  remove("file2")
  remove("file3")
  
  check(mtn("co", "--revision=tag"..i, "."), 0, false, false)
  check(base_revision() == revs[i])
  for j = 1,3 do
    check(exists("file"..j) == x[j])
  end
end
