
mtn_setup()

-- Generate the general expected output, as we're testing using the
-- output of 'list --help'
check(raw_mtn("list", "--help"), 0, true, false)
rename("stdout", "expout")

-- Check that --xargs works at all
writefile("at_test.input", "--help")
check(raw_mtn("list", "--xargs=at_test.input"), 0, {"expout"}, false)

-- The rest of the checks use the short form, -@

-- Check that -@ works at all
writefile("at_test.input", "--help")
check(raw_mtn("list", "-@", "at_test.input"), 0, {"expout"}, false)

-- Check that -@ works when injected in the middle of the arguments
-- (i.e. that it's prepended correctly)
writefile("at_test.input", "list")
check(raw_mtn("-@", "at_test.input", "--help"), 0, {"expout"}, false)

-- Check that -@ works when used more than once
writefile("at_test.input1", "list")
writefile("at_test.input2", "--help")
check(raw_mtn("-@", "at_test.input1", "-@", "at_test.input2"), 0, {"expout"}, false)

-- Check that -@ works with an argument file with no content
check(raw_mtn("list"), 1, true, false)
rename("stdout", "expout")
writefile("at_test.input")
check(raw_mtn("list", "-@", "at_test.input"), 1, {"expout"}, false)
