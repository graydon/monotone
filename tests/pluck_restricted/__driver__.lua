mtn_setup()

function C(str)
   return (string.gsub(str, "(.)", "%1\n"))
end

addfile("testfile", C("abc"))
addfile("otherfile", C("123"))
addfile("unchanging", "asdf\n")
commit()
root_rev = base_revision()

writefile("testfile", C("1bc"))
writefile("otherfile", C("a23"))
commit()
first_rev = base_revision()

writefile("testfile", C("1b3"))
writefile("otherfile", C("a2c"))
commit()
second_rev = base_revision()

revert_to(root_rev)
check(mtn("pluck", "-r", second_rev, "unchanging"), 1, false, true)
check(qgrep("no changes", "stderr"))
check(readfile("testfile") == C("abc"))
check(readfile("otherfile") == C("123"))

revert_to(root_rev)
check(readfile("testfile") == C("abc"))
check(readfile("otherfile") == C("123"))
check(mtn("pluck", "-r", second_rev, "testfile"), 0, false, false)
check(readfile("testfile") == C("ab3"))
check(readfile("otherfile") == C("123"))

revert_to(root_rev)
check(readfile("testfile") == C("abc"))
check(readfile("otherfile") == C("123"))
check(mtn("pluck", "-r", second_rev, "--exclude", "testfile"), 0, false, false)
check(readfile("testfile") == C("abc"))
check(readfile("otherfile") == C("12c"))
