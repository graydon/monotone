
skip_if(ostype == "Windows")
skip_if(not existsonpath("stat"))
mtn_setup()

addfile("foo", "blah blah")
addfile("bar", "blah blah")
check(mtn("attr", "set", "foo", "mtn:execute", "true"))
commit()
R=base_revision()

posix_umask(077)

-- log
L(posix_umask(077), "\n")
check(mtn("co", "-r", R, "077-co"), 0, false, false)
check(is_executable("077-co/foo"))
check(not is_executable("077-co/bar"))
check({"ls", "-l", "077-co/foo"}, 0, true, false)
check(string.find(readfile("stdout"), "-rwx------") == 1)


-- Don't do this one; it makes the directories also 200, which
-- kinda breaks things.

--posix_umask(577)

-- log
--L(posix_umask(577), "\n")
--xfail(mtn("co", "-r", R, "577-co"), 0, false, false)
--check(not is_executable("577-co/foo"))
--check(not is_executable("577-co/bar"))
--check(stat -c '%a' 577-co/foo, 0, [200
--])
--check(stat -c '%a' 577-co/bar, 0, [200
--])
