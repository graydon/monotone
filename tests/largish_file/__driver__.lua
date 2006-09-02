
mtn_setup()

-- Check that we can handle a 15 meg file in the database

-- This is only 'largish" -- we should check for >4 gigabytes too, for
-- a real "large file", but that would be kind of rude from the test
-- suite.

largish = io.open("largish", "wb")
str16k = string.rep("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 1024)
for i=1,15 do
  for j=1,64 do
    largish:write(str16k)
  end
end
largish:close()

check(mtn("add", "largish"), 0, false, false)
commit()
base = base_revision()

rename("largish", "largish.orig")

check(cat("-", "largish.orig"), 0, true, false, "foo\n")
rename("stdout", "largish")
append("largish", "bar\n")
commit()
mod = base_revision()

rename("largish", "largish.modified")

check(mtn("checkout", "--revision", base, "base"), 0, false, false)
check(samefile("largish.orig", "base/largish"))
check(mtn("checkout", "--revision", mod, "modified"), 0, false, false)
check(samefile("largish.modified", "modified/largish"))
