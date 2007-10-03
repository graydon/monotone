
mtn_setup()

addfile("foo", "the foo file")
addfile("bar", "the bar file")
addfile("baz", "the baz file")
commit()

check(mtn("ls", "changed"), 0, "")

check(mtn("drop", "foo"), 0, false, false)
check(mtn("rename", "bar", "bartender"), 0, false, false)
check(mtn("ls", "changed"), 0, true, 0)
check(samelines("stdout", {"bartender", "foo"}))
commit()

check(mtn("ls", "changed"), 0, "")

writefile("baz", "the baz file, modified")
check(mtn("ls", "changed"), 0, true, 0)
check(samelines("stdout", {"baz"}))
commit()

check(mtn("ls", "changed"), 0, "")

addfile("piano", "earplugs")
mkdir("guitar")
check(mtn("add", "guitar"), 0, false, false)
check(mtn("ls", "changed"), 0, true, 0)
check(samelines("stdout", {"guitar", "piano"}))

commit()
check(mtn("ls", "changed"), 0, "")
