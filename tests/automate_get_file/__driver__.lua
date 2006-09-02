
mtn_setup()

writefile("expected", "blah\n")

addfile("foo", "blah\n")
check(mtn("commit", "--date=2005-05-21T12:30:51",
          "--branch=testbranch", "--message=blah-blah"), 0, false, false)
rev = base_revision()
file = "4cbd040533a2f43fc6691d773d510cda70f4126a"

-- check that a correct usage produces correctly formatted output
check(mtn("automate", "get_file", file), 0, true, false)
canonicalize("stdout")
check(samefile("expected", "stdout"))

-- ensure that missing revisions fail
check(mtn("automate", "get_file", string.rep("0", 40)), 1, true, false)
check(fsize("stdout") == 0)

-- ensure that revisions are not being completed
check(mtn("automate", "get_file", string.sub(file, 1, 30)), 1, true, false)
check(fsize("stdout") == 0)
