
mtn_setup()
revs = {}

check(get("expected"))

writefile("empty", "")

addfile("foo", "blah")
check(mtn("commit", "--date=2005-05-21T12:30:51", "--branch=testbranch",
          "--message=blah-blah"), 0, false, false)
base = base_revision()

-- check that a correct usage produces correctly formatted output
check(mtn("automate", "certs", base), 0, true, false)
canonicalize("stdout")
check(samefile("expected", "stdout"))

-- ensure that missing revisions fail
check(mtn("automate", "certs", string.rep("0", 40)), 1, true, false)
canonicalize("stdout")
check(samefile("empty", "stdout"))

-- ensure that revisions are not being completed
check(mtn("automate", "certs", string.sub(base, 1, 30)), 1, true, false)
canonicalize("stdout")
check(samefile("empty", "stdout"))
