
mtn_setup()

-- This test relies on file-suturing

mkdir("zz")

addfile("ancfile", "ancestral file")
addfile("zz/testfile0", "added file")
commit()
anc = base_revision()


addfile("zz/testfile1", "added file")
commit()

remove("zz")
revert_to(anc)

writefile("ancfile", "changed anc")

addfile("zz/testfile1", "added file")

commit()

xfail_if(true, mtn("--branch=testbranch", "merge"), 0, false, false)
check(mtn("update"), 0, false, false)

merged = base_revision()

check(mtn("automate", "get_revision", merged), 0, true)
rename("stdout", "rev")
check(not qgrep("add_file", "rev"))
