
mtn_setup()

-- Invoking a group fails with an appropriate error message.
check(mtn("review"), 1, "", true)
output = readfile("stderr")
check(string.find(output, "is invalid; it is a group") ~= nil)

-- Invoking an empty group fails with an appropriate error message.
check(mtn("user"), 1, "", true)
output = readfile("stderr")
check(string.find(output, "is invalid; it is a group") ~= nil)

-- Command completion does not work on groups.
check(mtn("revie"), 1, "", true)
output = readfile("stderr")
check(string.find(output, "unknown command") ~= nil)
