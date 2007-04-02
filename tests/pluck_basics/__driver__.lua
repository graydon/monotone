mtn_setup()

addfile("testfile", "1\n2\n3\n4\n5\n")
commit()
root_rev = base_revision()

writefile("testfile", "1-changed\n2\n3\n4\n5\n")
commit()
first_rev = base_revision()

writefile("testfile", "1-changed\n2\n3-changed\n4\n5\n")
addfile("somefile", "blah blah\n")
commit()
second_rev = base_revision()

revert_to(root_rev)
remove("somefile")
check(mtn("rename", "testfile", "otherfile"), 0, false, false)
writefile("otherfile", "1\n2\n3\n4\n5-changed\n")

check(mtn("pluck", "-r", second_rev), 0, false, false)
newtext = readfile("otherfile")
check(newtext == "1\n2\n3-changed\n4\n5-changed\n")
check(readfile("somefile") == "blah blah\n")
check(string.find(readfile("_MTN/log"), first_rev .. ".*" .. second_rev) ~= nil)
writefile("_MTN/log", "")

check(mtn("pluck", "-r", second_rev, "-r", first_rev), 0, false, false)
newtext = readfile("otherfile")
check(newtext == "1\n2\n3\n4\n5-changed\n")
-- this should _not_ have deleted "somefile", even though the second->first
-- transition deletes "somefile", because the identity link between these two
-- files with the name "somefile" has been broken.
check(exists("somefile"))
check(string.find(readfile("_MTN/log"), second_rev .. ".*" .. first_rev) ~= nil)
writefile("_MTN/log", "")

-- should get a conflict on the two "somefile" adds
check(mtn("pluck", "-r", root_rev, "-r", second_rev), 1, false, false)
check(readfile("_MTN/log") == "")

check(mtn("drop", "somefile"), 0, false, false)
remove("somefile")
-- now it should work again
check(mtn("pluck", "-r", root_rev, "-r", second_rev), 0, false, false)
newtext = readfile("otherfile")
check(newtext == "1-changed\n2\n3-changed\n4\n5-changed\n")
check(readfile("somefile") == "blah blah\n")
check(string.find(readfile("_MTN/log"), root_rev .. ".*" .. second_rev) ~= nil)
writefile("_MTN/log", "")
