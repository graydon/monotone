mtn_setup()
revs = {}

get("expected")

writefile("empty", "")

addfile("foo", "blah")
check(mtn("commit", "--date=2005-05-21T12:30:51", "--branch=testbranch",
          "--message=blah-blah"), 0, false, false)
base = base_revision()

check(mtn("automate", "cert", base, "testcert", "testvalue"), 0, true, false)
check(samefile("empty", "stdout"))

-- check that a correct usage produces correctly formatted output
check(mtn("automate", "certs", base), 0, true, false)
canonicalize("stdout")
check(samefile("expected", "stdout"))

-- check that 'cert' gets keydir from workspace options when run via stdio
check(mtn_ws_opts("automate", "stdio"), 0, true, true, "l4:cert40:" .. base .. "9:testcert23:fooe")
check("0:0:l:0:" == readfile("stdout"))
check(samefile("empty", "stderr"))

-- check edge cases:
-- wrong number of arguments:
check(mtn("automate", "cert", base, "asdf"), 1, false, false)
check(mtn("automate", "cert", base, "testcert", "testvalue", "asdf"), 1, false, false)
-- nonexistent revision:
check(mtn("automate", "cert", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
          "branch", "net.venge.monotone"),
      1, false, false)
