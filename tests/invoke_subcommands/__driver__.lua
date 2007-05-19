
mtn_setup()

-- Invoking a command with an unknown subcommand shows the correct error.
check(mtn("list", "foobar"), 1, "", true)
output = readfile("stderr")
check(string.find(output, "could not match 'foobar'") ~= nil)
check(string.find(output, "to a subcommand of 'list'") ~= nil)

-- Invoking a command with a missing subcommand shows the correct error.
check(mtn("list"), 1, "", true)
output = readfile("stderr")
check(string.find(output, "no subcommand specified for 'list'") ~= nil)

-- Invoking a command with a correct subcommand works.
check(mtn("list", "keys"), 0, false, "")
