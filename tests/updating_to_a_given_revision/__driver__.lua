
mtn_setup()

getfile("root")
getfile("middle")
getfile("left-leaf")
getfile("right-leaf")
getfile("modified-left-leaf")
getfile("modified-root")

revs = {}

-- Create root revision.
copyfile("root", "testfile")
check(cmd(mtn("add", "testfile")), 0, false, false)
commit()
revs.root = base_revision()

-- Create middle revision based off root.
copyfile("middle", "testfile")
commit()
revs.middle = base_revision()

-- Create leaf revision based off middle.
copyfile("left-leaf", "testfile")
commit()
revs.left = base_revision()

-- Test going backward in the revision tree.
check(cmd(mtn("update", "--revision", revs.root)), 0, false, false)
check(samefile("testfile", "root"))

-- Test going forward in the revision tree.
check(cmd(mtn("update", "--revision", revs.middle)), 0, false, false)
check(samefile("testfile", "middle"))

-- Create a fork from middle.
copyfile("right-leaf", "testfile")
commit()
revs.right = base_revision()

-- Test going from the right left to the left leaf via the common ancestor.
check(cmd(mtn("update", "--revision", revs.left)), 0, false, false)
check(samefile("testfile", "left-leaf"))

-- Test that workspace changes are kept while going backward.
copyfile("modified-left-leaf", "testfile")
check(cmd(mtn("update", "--revision", revs.root)), 0, false, false)
check(samefile("testfile", "modified-root"))
