
mtn_setup()

-- I don't get it. This seems to work, but WTF is that qgrep looking for?

-- decode first file and commit to db
check(get("firstfile", "testfile"))
addfile("testfile")
commit()
rename("testfile", "firstfile")

-- calculate diff to second file using monotone
check(get("secondfile", "testfile"))
check(mtn("diff"), 0, true)
rename("stdout", "monodiff")

-- look for a meaningless change
check(not qgrep("^-$", "monodiff"))
