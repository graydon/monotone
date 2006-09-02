
mtn_setup()

writefile("testfile", "version 0 of the file\n")
check(mtn("add", "testfile"), 0, false, false)
commit()

fsha = {}
rsha = {}

for i = 1, 6 do
  L(string.format("generating version %i of the file\n", i))
  writefile("testfile", string.format("version %i of the file\n", i))
  commit()
  fsha[i] = sha1("testfile")
  rsha[i] = base_revision()
end

for i = 1, 6 do
  L(string.format("checking version %i of the file\n", i))
  writefile("testfile", string.format("version %i of the file\n", i))
  check(mtn("automate", "get_file", fsha[i]), 0, true)
  canonicalize("stdout")
  check(samefile("stdout", "testfile"))
  remove("_MTN")
  check(mtn("checkout", "--revision", rsha[i], "."), 0, true)
  check(sha1("testfile") == fsha[i])
end
