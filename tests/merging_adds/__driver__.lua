
mtn_setup()

addfile("irrelevantfile", "this is just a file\n")
commit()
anc = base_revision()

addfile("testfile1", "This is test file 1\n")
commit()
left = base_revision()

revert_to(anc)
remove("testfile1")

addfile("testfile2", "This is test file 2\n")
commit()
right = base_revision()

check(mtn("--branch=testbranch", "merge"), 0, false, false)
check(mtn("update"), 0, false, false)

writefile("expected_irrelevant", "this is just a file\n")
writefile("expected_data1", "This is test file 1\n")
writefile("expected_data2", "This is test file 2\n")

check(samefile("irrelevantfile", "expected_irrelevant"))
check(samefile("testfile1", "expected_data1"))
check(samefile("testfile2", "expected_data2"))
