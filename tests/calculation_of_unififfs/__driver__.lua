
skip_if(not existsonpath("patch"))

mtn_setup()

-- get first file and commit to db
getfile("firstfile", "testfile")
check(cmd(mtn("add", "testfile")), 0, false, false)
commit()
os.rename("testfile", "firstfile")

-- get second file
getfile("secondfile", "testfile")

-- calculate diff to second file using monotone
check(cmd(mtn("diff")), 0, true)
canonicalize("stdout")
os.rename("stdout", "monodiff")
os.rename("testfile", "secondfile")

-- see if patch likes that
os.rename("monodiff", "stdin")
check(cmd("patch", "firstfile"), 0, false, false, true)

-- see if the resulting file has been properly patched
check(samefile("firstfile", "secondfile"))
