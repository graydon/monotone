
mtn_setup()
revs = {}

addfile("foo", "foo")
addfile("bar", "bar")

commit()
revs[1] = base_revision()

mkdir("dir1")
mkdir("dir2")
addfile("dir1/file1", "dir1/file1")

commit()
revs[2] = base_revision()

addfile("dir2/file2", "dir2/file2")

commit()
revs[3] = base_revision()

writefile("foo", "foofoo")
writefile("bar", "barbar")
writefile("dir1/file1", "dir1/file1 asdf")
writefile("dir2/file2", "dir2/file2 asdf")

commit()
revs[4] = base_revision()

writefile("dir1/file1", "dir1/file1 asdf asdf")

commit()
revs[5] = base_revision()

writefile("dir2/file2", "dir2/file2 asdf asdf")

commit()
revs[6] = base_revision()

check(mtn("attr", "set", "dir2/", "myattr", "myval"), 0, false, false)
commit()
revs[7] = base_revision()

for n,x in pairs{[""]  = {0,0,0,0,0,0,0},
                 ["."] = {0,0,0,0,0,0,0},
                 dir1  = {1,0,1,0,0,1,1},
                 dir2  = {1,1,0,0,1,0,0}} do
  if n == "" then
    check(mtn("log"), 0, true)
  else
    check(mtn("log", n), 0, true)
  end
  for i,v in pairs(x) do
    L("Checking log of '", n, "' for revision ", i, "\n")
    check((v == 0) == qgrep("^[\\|\\\\\/ ]+Revision: "..revs[i], "stdout"))
  end
end
