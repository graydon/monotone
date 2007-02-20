
mtn_setup()

addfile("foo", "foo")
commit()
foo = base_revision()

addfile("bar", "bar")
commit()

addfile("baz", "baz")
commit()


check(mtn("log", "--last=0"), 1, 0, false)

for i = 1,3 do
  check(mtn("log", "--last="..i), 0, true)
  check(grep("^[\\|\\\\\/ ]+Revision:", "stdout"), 0, true)
  check(numlines("stdout") == i)
end

revert_to(foo)


check(mtn("log", "--next=0"), 1, 0, false)

for i = 1,3 do
  check(mtn("log", "--next="..i), 0, true)
  check(grep("^[\\|\\\\\/ ]+Revision:", "stdout"), 0, true)
  check(numlines("stdout") == i)
end
