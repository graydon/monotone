
mtn_setup()

-- I don't get it. This seems to work, but WTF is that qgrep looking for?

-- decode first file and commit to db
getfile("firstfile", "testfile")
addfile("testfile")
commit()
os.rename("testfile", "firstfile")

-- calculate diff to second file using monotone
getfile("secondfile", "testfile")
check(cmd(mtn("diff")), 0, true)
os.rename("stdout", "monodiff")

-- look for a meaningless change
check(not qgrep("^-$", "monodiff"))
