
mtn_setup()
revs = {}

addfile("a", "some data")
commit()
revs.a = base_revision()

check(mtn("rename", "--bookkeep-only", "a", "b"), 0, false, false)
rename("a", "b")
commit()
revs.b = base_revision()

check(mtn("drop", "--bookkeep-only", "b"), 0, false, false)
remove("b")
commit()
revs.null = base_revision()

for _,x in pairs{{revs.a, true, false},
                 {revs.b, false, true},
                 {revs.null, false, false}} do
  remove("_MTN")
  check(mtn("checkout", "--revision", x[1], "co-dir"), 0, false, false)
  check(exists("co-dir/a") == x[2])
  check(exists("co-dir/b") == x[3])
  remove("co-dir")
end
