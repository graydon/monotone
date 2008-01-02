-- -*- lua -*-

skip_if(not existsonpath("patch"))

mtn_setup()

-- get first file and commit to db
get("firstfile", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()
rename("testfile", "firstfile")

-- get second file
get("secondfile", "testfile")

-- calculate diff to second file using monotone
check(mtn("diff"), 0, true)
canonicalize("stdout")
rename("stdout", "monodiff")
rename("testfile", "secondfile")

-- see if patch likes that
rename("monodiff", "stdin")
xfail({"patch", "firstfile"}, 0, false, false, true)

-- see if the resulting file has been properly patched
check(samefile("firstfile", "secondfile"))
