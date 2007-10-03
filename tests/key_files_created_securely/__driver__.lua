-- FIXME implement for Windows.
skip_if(ostype == "Windows")
skip_if(not existsonpath("stat"))

function quote_for_shell(argv)
   ret = ""
   for _,arg in ipairs(argv) do
      ret = ret .. " '" .. string.gsub(arg, "'", "'\\''") .. "'"
   end
   return ret
end

mtn_setup()

cmd = quote_for_shell(raw_mtn("--keydir=keys", "genkey", "foobar"))

-- force a permissive umask to be in effect
check({ "sh", "-c", "umask 0000; exec" .. cmd },
      0, false, false, string.rep("foobar\n", 2))

check({ "ls", "-l", "keys/foobar" }, 0, true, nil)
check(qgrep("^-rw------- .*keys/foobar", "stdout"))
