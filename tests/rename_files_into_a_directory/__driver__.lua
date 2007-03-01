
mtn_setup()

addfile("gnat", "gnat")
addfile("mosquito", "mosquito")
addfile("termite", "termite")
addfile("ant", "ant")

-- will force foo/, bar/ and foo/gnat/ to be created
mkdir("foo")
mkdir("bar")
mkdir("foo/gnat")
addfile("foo/dummy", "... ... ...")
addfile("bar/dummy", "a b c")
addfile("foo/gnat/dummmy", "la la la")

commit()

-- checkout in a clean dir and cd there
function co()
  remove(test.root.."/checkout")
  check(mtn("checkout", "-b", "testbranch", test.root.."/checkout"),
        0, false, false)
end

co()

-- basics
check(indir("checkout", mtn("rename", "--bookkeep-only", "ant", "foo")), 0, false, false)
check(indir("checkout", mtn("rename", "--bookkeep-only", "mosquito", "termite", "foo")),
      0, false, false)

co()

-- with --execute
check(indir("checkout", mtn("rename", "ant", "foo")), 0, false, false)
check(indir("checkout", mtn("rename", "mosquito", "termite", "foo")),
      0, false, false)
for _,x in pairs{"ant", "mosquito", "termite"} do
  check(exists("checkout/foo/"..x))
  check(not exists("checkout/"..x))
end

-- to root
check(indir("checkout", mtn("rename", "foo/ant", ".")),
      0, false, false)
check(indir("checkout", mtn("rename", "--bookkeep-only", "foo/termite", ".")), 0, false, false)
check(exists("checkout/ant") and exists("checkout/foo/termite"))
check(not exists("checkout/foo/ant") and not exists("checkout/termite"))

co()

-- conflicts
check(indir("checkout", mtn("rename", "--bookkeep-only", "gnat", "foo")), 1, false, false)
check(indir("checkout", mtn("rename", "--bookkeep-only", "gnat", "termite", "foo")), 1, false, false)
check(indir("checkout", mtn("rename", "--bookkeep-only", "termite", "foo")), 0, false, false)

check(indir("checkout", mtn("rename", "--bookkeep-only", "mosquito", "foo/ant")), 0, false, false)
check(indir("checkout", mtn("rename", "--bookkeep-only", "ant", "foo")), 1, false, false)

co()

check(indir("checkout", mtn("rename", "gnat", "foo")), 1, false, false)
check(indir("checkout", mtn("rename", "gnat", "termite", "foo")), 1, false, false)
check(indir("checkout", mtn("rename", "termite", "foo")), 0, false, false)
check(exists("checkout/foo/termite") and not exists("checkout/termite"))
check(exists("checkout/gnat") and exists("checkout/foo/gnat/."))
check(not exists("foo/gnat/gnat"))

co()

---- TODO: 
-- issues with missing files, should usually be allowed?
-- rename to self
-- rename root node

-- rename to non-existing dir path: "foo->blweorih/o4thoihs" (this isn't a destdir case, but needs testing somewhere).

-- file0->bar, file0 doesn't exist

-- file0->bar, file0 exists, -e

-- file0->bar, file0 doesn't exist, -e

-- check that nothing happens if any would fail
-- file0->bar file1->bar, file0 exists, file1 doesn't

-- file0->bar file1->bar, file0 exists, file1 doesn't, -e

-- file0->bar, bar/file0 exists in working, file0 doesn't -e

-- file0->bar
