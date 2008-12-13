
mtn_setup()
check(get("test.lua"))

function equalRecursive(left, right)
    if (type(left) ~= type(right)) then
        L("type mismatch: ", type(left), " vs ", type(right), "\n")
        return false
    end

    if (type(left) == "table") then
        for k,v in ipairs(left) do
            -- a not existing key is a key with its value set to nil
            if (right[k] == nil) then
                L("key ", k, " does not exist in right", "\n")
                return false
            end
            if (not equalRecursive(left[k], right[k])) then
                return false
            end
        end
        return true
    end

    return left == right
end

function echo(retval, err, ...)
    local args = {n=select('#', ...), ... }
    check(mtn("automate", "lua", "--rcfile=test.lua", "echo", unpack(args, 1, args.n)), retval, true, true)
    if (retval == 0) then
        -- actually we're doing something here which the std_hook does itself
        -- as well - might not be a good test...?
        for i=1,args.n do
            args[i] = assert(loadstring("return " .. args[i]))()
        end
        args.n = nil
        canonicalize("stdout")
        local out = readfile("stdout")
        local echoed = assert(loadstring("return { "..out.."}"))();
        check(equalRecursive(args, echoed))
    else
        canonicalize("stderr")
        check(grep(err, "stderr"), 0, false, false)
    end
end

-- testing simple types
echo(0, nil, 'nil')
echo(0, nil, '3')
echo(0, nil, '3.4')
echo(0, nil, 'true')
echo(0, nil, 'false')
-- different quoting style
echo(0, nil, "'foo'")
echo(0, nil, '"foo"')

-- testing tables and nesting
echo(0, nil, "{1,2,3}")
echo(0, nil, "{true,false,{1,2,{'foo','bar'}}}")
echo(0, nil, "{[true] = false; ['foo'] = 'bar'; 3 }")

-- multiple arguments
echo(0, nil, 'nil', '3', '3.4', "'foo'", '"foo"', 'true', 'false', "{1,2,3}")

-- this is an unknown variable name which gets evaluated to nil
echo(1, "was evaluated to nil", "unknown")

-- this is an invalid argument (missing bracket)
echo(1, "could not be evaluated", "{1,2,3")

-- and finally, call a function which does not exist at all
check(mtn("automate", "lua", "foo"), 1, false, true)
canonicalize("stderr")
check(grep("lua function 'foo' does not exist", "stderr"), 0, false, false)

