mtn_setup()

addfile("foo", "blah blah")
commit("mybranch")

testURI="file:" .. test.root .. "/test.db"

check(nodb_mtn("clone", "-b", "mybranch", testURI), 0, false, false)
check(exists("mybranch"))
check(readfile("foo") == readfile("mybranch/foo"))

-- but now that that directory exists, this clone should fail
check(nodb_mtn("clone", "-b", "mybranch", testURI), 1, false, false)

-- but succeed if given a specific dir
check(nodb_mtn("clone", "-b", "mybranch", testURI, "otherdir"), 0, false, false)

-- clone into . should also fail, unlike checkout
mkdir("test4")
check(indir("test4", nodb_mtn("clone", "-b", "mybranch", testURI, ".")), 1, false, false)
