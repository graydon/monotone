
mtn_setup()
save_env()

check(get("persist.lua"))

addfile("input.txt", "version 0 of the file")
writefile("input.txt", "version 1 of the file")

check(mtn("--ssh-sign=no", "--branch=testbranch", "--rcfile=persist.lua",
          "commit", "--message=blah-blah"),
      1, false, false, "tester@test.net\n")

check(mtn("--ssh-sign=no", "--branch=testbranch", "--rcfile=persist.lua",
          "commit", "--message=blah-blah"),
      0, false, false, string.rep("tester@test.net\n", 4))

check(mtn("ls", "certs", base_revision()), 0, true)
check(qgrep("branch", "stdout"))
check(qgrep("author", "stdout"))
check(qgrep("date", "stdout"))
check(qgrep("changelog", "stdout"))

