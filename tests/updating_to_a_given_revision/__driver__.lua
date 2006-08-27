
mtn_setup()

check(get("root"))
check(get("middle"))
check(get("left-leaf"))
check(get("right-leaf"))
check(get("modified-left-leaf"))
check(get("modified-root"))

revs = {}

-- Create root revision.
copy("root", "testfile")
check(mtn("add", "testfile"), 0, false, false)
commit()
revs.root = base_revision()

-- Create middle revision based off root.
copy("middle", "testfile")
commit()
revs.middle = base_revision()

-- Create leaf revision based off middle.
copy("left-leaf", "testfile")
commit()
revs.left = base_revision()

-- Test going backward in the revision tree.
check(mtn("update", "--revision", revs.root), 0, false, false)
check(samefile("testfile", "root"))

-- Test going forward in the revision tree.
check(mtn("update", "--revision", revs.middle), 0, false, false)
check(samefile("testfile", "middle"))

-- Create a fork from middle.
copy("right-leaf", "testfile")
commit()
revs.right = base_revision()

-- Test going from the right left to the left leaf via the common ancestor.
check(mtn("update", "--revision", revs.left), 0, false, false)
check(samefile("testfile", "left-leaf"))

-- Test that workspace changes are kept while going backward.
copy("modified-left-leaf", "testfile")
check(mtn("update", "--revision", revs.root), 0, false, false)
check(samefile("testfile", "modified-root"))
