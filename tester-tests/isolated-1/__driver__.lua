
-- functions can be redefined
foo = "bar"
old_L = L
L = function () unlogged_mkdir("xxx") end
mkdir("bar") -- calls L()
L = old_L
check(exists("xxx"))

-- part 1: edit some globals for the next test...
foo = "bar"
L = nil
