-- In this test, we put things in .mtn-ignore that trigger every
-- possible syntax error message from the regular expression library,
-- to ensure that the user's view of these errors is sensible.

mtn_setup()

writefile("ignoreme")
writefile("dontignoreme")
check(get("mtn-ignore", ".mtn-ignore"))

check(raw_mtn("ls", "unknown"), 0, true, true)
check(get("stdout-ref"))
check(get("stderr-ref"))

check(samefile("stdout", "stdout-ref"))
-- the first line of stderr may vary from run to run
check(tailfile("stderr", 1) == readfile("stderr-ref"))
