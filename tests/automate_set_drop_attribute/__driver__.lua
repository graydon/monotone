
mtn_setup()

-- path / arg checks
check(mtn("automate", "set_attribute"), 1, false, true)
check(qgrep("wrong argument count", "stderr"))
check(mtn("automate", "set_attribute", "too", "many", "many", "args"), 1, false, true)
check(qgrep("wrong argument count", "stderr"))

check(mtn("automate", "drop_attribute"), 1, false, true)
check(qgrep("wrong argument count", "stderr"))
check(mtn("automate", "drop_attribute", "too", "many", "args"), 1, false, true)
check(qgrep("wrong argument count", "stderr"))

check(mtn("automate", "set_attribute", "unknown_path", "foo", "bar"), 1, false, true)
check(qgrep("Unknown path", "stderr"))
check(mtn("automate", "drop_attribute", "unknown_path"), 1, false, true)
check(qgrep("Unknown path", "stderr"))

-- check if we can add an attribute
addfile("testfile", "foo")
check(mtn("automate", "set_attribute", "testfile", "foo", "bar"), 0, false, false)

-- check if it has been really added
check(mtn("automate", "get_attributes", "testfile"), 0, true, false)
parsed = parse_basic_io(readfile("stdout"))

check(table.getn(parsed) == 3)
for _,l in pairs(parsed) do
    if l.name == "attr" then 
        key = l.values[1]
        val = l.values[2]
        check(key == "foo" and val == "bar")
    end
    if l.name == "state" then
        state = l.values[1]
        check(state == "added")
    end
end

-- check if we can drop it
check(mtn("automate", "drop_attribute", "testfile", "foo"), 0, true, true)

-- check if it has been really dropped
check(mtn("automate", "get_attributes", "testfile"), 0, true, false)
parsed = parse_basic_io(readfile("stdout"))
check(table.getn(parsed) == 1)

-- check if it escalates properly if there is no such attr to drop
check(mtn("automate", "drop_attribute", "testfile", "foo"), 1, false, true)
check(qgrep("does not have attribute", "stderr"))

