
mtn_setup()
revs = {}

addfile("a", "some data")
commit()
revs.a = base_revision()

check(cmd(mtn("rename", "a", "b")), 0, false, false)
os.rename("a", "b")
commit()
revs.b = base_revision()

check(cmd(mtn("drop", "b")), 0, false, false)
remove("b")
commit()
revs.null = base_revision()

for _,x in pairs{{revs.a, true, false},
                 {revs.b, false, true},
                 {revs.null, false, false}} do
  remove_recursive("_MTN")
  check(cmd(mtn("checkout", "--revision", x[1], "co-dir")), 0, false, false)
  check(exists("co-dir/a") == x[2])
  check(exists("co-dir/b") == x[3])
  remove_recursive("co-dir")
end
