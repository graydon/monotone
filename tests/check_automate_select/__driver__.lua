
mtn_setup()
revs = {}

addfile("testfile", "this is just a file")
copy("testfile", "testfile1")
commit()
revs.first = base_revision()

writefile("testfile", "Now, this is a different file")
copy("testfile", "testfile2")
commit()
revs.second = base_revision()

writefile("testfile", "And we change it a third time")
copy("testfile", "testfile3")
commit()

check(mtn("cert", revs.first, "testcert", 'value=with=equal=signs'))
check(mtn("cert", revs.second, "testcert", 'value'))

-- Check that inexact values fail...
check(mtn("automate", "select", "c:testcert=value="), 0, true, false)
check(numlines("stdout") == 0)

-- Check that wild cards succeed (this one becomes a misuse, because it will
-- match two revisions)...
check(mtn("automate", "select", "c:testcert=value*"), 0, true, false)
check(numlines("stdout") == 2)

-- Check that no value succeeds...
check(mtn("automate", "select", "c:testcert"), 0, true, false)
check(numlines("stdout") == 2)

-- Check that exact value succeed...
check(mtn("automate", "select", "c:testcert=value"), 0, true, false)
check(numlines("stdout") == 1)
