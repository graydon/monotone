
mtn_setup()

addfile("testfile", "blah blah")
commit()
r0 = base_revision()

writefile("testfile", "stuff stuff")
commit()
r1 = base_revision()

check(mtn("checkout", "--branch=testbranch", "--revision", r0, "td"), 0, false, true)
writefile("td/_MTN/inodeprints")
check(indir("td", mtn("update")), 0, false, false)
check(fsize("td/_MTN/inodeprints") ~= 0)
