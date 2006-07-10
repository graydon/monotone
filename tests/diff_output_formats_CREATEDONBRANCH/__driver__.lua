function test_one(base)
   local src = base .. ".src"
   local dst = base .. ".dst"
   local ud  = base .. ".ud"
   local cd  = base .. ".cd"
   local udp = base .. ".udp"
   local cdp = base .. ".cdp"
   if not get(src) or not get(dst) then
      error("case '" .. base .. "': mandatory component missing", 2)
      return
   end
   check(mtn("fload"), 0, nil, nil, {src})
   check(mtn("fload"), 0, nil, nil, {dst})
   src = sha1(src)
   dst = sha1(dst)

   if get(ud) then
      check(mtn("fdiff", base, base, src, dst), 0, {ud}, nil, nil)
   end
   if get(cd) then
      check(mtn("fdiff", "-c", base, base, src, dst), 0, {cd}, nil, nil)
   end
   if get(udp) then
      check(mtn("fdiff", "-p", base, base, src, dst), 0, {udp}, nil, nil)
   end
   if get(cdp) then
      check(mtn("fdiff", "-c", "-p", base, base, src, dst), 0, {cdp}, nil, nil)
   end
end

mtn_setup()

-- We do this first so that we can test per-file patterns.
append("test_hooks.lua",
       "function get_encloser_pattern(name)\n"..
       "  if name == \"hello\" then\n"..
       "    return \"^[[:alnum:]$_]\"\n"..
       "  else\n"..
       "    return \"-- initial\"\n"..
       "  end\n"..
       "end\n")

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
