function test_one(base)
   local src = base .. ".src"
   local dst = base .. ".dst"
   local ud  = base .. ".ud"
   local cd  = base .. ".cd"
   local udp = base .. ".udp"
   local cdp = base .. ".cdp"
   if not get(src) or not get(dst) 
      or not get(ud) or not get(cd) 
      or not get(udp) or not get(cdp)
   then
      error("case '" .. base .. "': missing file", 2)
      return
   end
   check(mtn("fload"), 0, nil, nil, {src})
   check(mtn("fload"), 0, nil, nil, {dst})
   src = sha1(src)
   dst = sha1(dst)

   check(mtn("fdiff", "--no-show-encloser", base, base, src, dst), 0, true, nil, nil)
   canonicalize("stdout")
   check(samefile("stdout", ud))
   check(mtn("fdiff", "--context", "--no-show-encloser", base, base, src, dst),
         0, true, nil, nil)
   canonicalize("stdout")
   check(samefile("stdout", cd))
   check(mtn("fdiff", base, base, src, dst), 
         0, true, nil, nil)
   canonicalize("stdout")
   check(samefile("stdout", udp))
   check(mtn("fdiff", "--context", base, base, src, dst),
         0, true, nil, nil)
   canonicalize("stdout")
   check(samefile("stdout", cdp))
end

append("test_hooks.lua",
       "function get_encloser_pattern(name)\n"..
       "  if name == \"hello\" then\n"..
       "    return \"^[[:alnum:]$_]\"\n"..
       "  else\n"..
       "    return \"-- initial\"\n"..
       "  end\n"..
       "end\n")

mtn_setup()

test_one("hello")
test_one("A")
test_one("B")
test_one("C")
test_one("D")
test_one("E")
test_one("F")
test_one("G")
test_one("H")
test_one("I")
test_one("J")
