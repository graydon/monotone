
mtn_setup()

check(get("persist.lua"))

writefile("input.txt", "version 0 of the file\n")

check(mtn("add", "input.txt"), 0, false, false)

writefile("input.txt", "version 1 of the file\n")

check(mtn("--branch=testbranch", "--rcfile=persist.lua", "--message=blah-blah", "commit"), 0, false, false, "tester@test.net\n")

tsha = base_revision()

check(mtn("ls", "certs", tsha), 0, true)
rename("stdout", "certs")

for i,name in pairs{"branch", "author", "date", "changelog"} do
  check(qgrep(name, "certs"))
end
