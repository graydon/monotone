
-- part 2: ...check that those globals have been reset
check(foo == nil)
mkdir("abc")
check(not exists("xxx"))
L("...") -- "attempt to call nil value"
