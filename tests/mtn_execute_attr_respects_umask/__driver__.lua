
skip_if(ostype == "Windows")
mtn_setup()

-- I don't know why this fails!  When I set umask 077 and check out
-- this tree by hand, it works fine; when I run this test, though, then
-- for some reason the foo file is checked out with permission 600!

addfile("foo", "blah blah")
addfile("bar", "blah blah")
check(mtn("attr", "set", "foo", "mtn:execute", "true"))
commit()
R=base_revision()

posix_umask(077)

-- log
L(posix_umask(077), "\n")
check(mtn("co", "-r", R, "077-co"), 0, false, false)
xfail_if(true, is_executable("077-co/foo"))
check(not is_executable("077-co/bar"))
--check(stat -c '%a' 077-co/foo, 0, [700
--])
--check(stat -c '%a' 077-co/bar, 0, [600
--])

posix_umask(577)

-- log
L(posix_umask(577), "\n")
check(mtn("co", "-r", R, "577-co"), 0, false, false)
check(not is_executable("577-co/foo"))
check(not is_executable("577-co/bar"))
--check(stat -c '%a' 577-co/foo, 0, [200
--])
--check(stat -c '%a' 577-co/bar, 0, [200
--])
