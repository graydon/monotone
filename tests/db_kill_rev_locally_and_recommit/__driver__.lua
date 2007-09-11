mtn_setup()

addfile("base", "base")
commit()
base = base_revision()


-- create a new child of base with some new content and commit
mkdir("test1")
chdir("test1")
check(mtn("checkout", "--revision", base, "."))

mkdir("foo")
addfile("file1", "file1")
addfile("foo/file2", "foofile2")

commit()
new_rev1 = base_revision()

chdir("..")

-- kill_rev_locally the new child
check(mtn("db", "kill_rev_locally", new_rev1))

-- recreate and attempt to commit the same child again from base
mkdir("test2")
chdir("test2")
check(mtn("checkout", "--revision", base, "."))

mkdir("foo")
addfile("file1", "file1")
addfile("foo/file2", "foofile2")

-- this should just work
check(mtn("commit", "-m", "recommit same id"), 0, false, false)

