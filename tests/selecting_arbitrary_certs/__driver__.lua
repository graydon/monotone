
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
check(mtn("list", "certs", "'c:testcert=value='"), 1, false, true)
check(grep('no match for selection', "stderr"), 0, false)

-- Check that wild cards succeed...
check(mtn("list", "certs", "c:testcert=value=*"), 0, false, false)

-- Check that wild cards succeed (this one becomes a misuse, because it will
-- match two revisions)...
check(mtn("list", "certs", "c:testcert=value*"), 1, false, true)
check(grep('has multiple ambiguous expansions', "stderr"), 0, false)

-- Check that no value succeeds...
check(mtn("list", "certs", "c:testcert"), 1, false, true)
check(grep('has multiple ambiguous expansions', "stderr"), 0, false)

-- Check that exact value succeed...
remove("_MTN")
remove("testfile")
check(mtn("co", "--revision=c:testcert=value", "."), 0, false, false)
check(samefile("testfile", "testfile2"))

remove("_MTN")
remove("testfile")
check(mtn("co", "--revision=c:testcert=value=with=equal=signs", "."), 0, false, false)
check(samefile("testfile", "testfile1"))
