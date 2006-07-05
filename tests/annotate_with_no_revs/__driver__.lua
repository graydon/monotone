
mtn_setup()

addfile("testfile", "blah blah")

check(mtn("annotate", "testfile"), 1, false, false)
