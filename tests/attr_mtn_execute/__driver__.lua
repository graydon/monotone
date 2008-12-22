--
-- test setting/clearing of the the execute file attribute works
--
skip_if(ostype=="Windows")

mtn_setup()

writefile("foo", "some data")
check(mtn("add", "foo"), 0, false, false)
commit()
without_x = base_revision()

check(mtn("attr", "set", "foo", "mtn:execute", "true"), 0, false, false)
check({"test", "-x","foo"}, 0, false, false)
commit()
with_x = base_revision()

check(mtn("update", "-r"..without_x), 0, true, true)
-- expected to fail:
-- * we don't call hooks when an attribute is cleared
-- * we don't have a way to clear the x-bit from lua 
--   (that's easy to implement though)
xfail({"test", "!", "-x","foo"}, 0, false, false)

-- note: tests following an xfail are not executed

check(mtn("update", "-r "..with_x), 0, false, false)
check({"test", "-x","foo"}, 0, false, false)

