
mtn_setup()

addfile("testfile", "blah blah")

check(mtn("annotate", "--brief", "testfile"), 1, false, false)
