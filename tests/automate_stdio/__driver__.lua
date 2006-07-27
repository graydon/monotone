
mtn_setup()

function parse_stdio(dat, which)
  local got = {}
  while true do
    local b,e,n,s = string.find(dat, "(%d+):%d+:[lm]:(%d+):")
    if b == nil then break end
    n = n + 0
    if got[n] == nil then got[n] = "" end
    got[n] = got[n] .. string.sub(dat, e+1, e+s)
    dat = string.sub(dat, e+1+s)
  end
  if got[which] == nil then got[which] = "" end
  L("output of command ", which, ":\n")
  L(got[which])
  return got[which]
end

writefile("output", "file contents")
writefile("output2", "other contents")

check(mtn("automate", "inventory"), 0, true, false)
canonicalize("stdout")
rename("stdout", "output")
check(mtn("automate", "test"), 0, true, false, "ab def 8 xq")
canonicalize("stdout")
rename("stdout", "output2")
check(mtn("automate", "stdio", "--debug"), 0, true, true, "l9:inventorye o4:fork5:spoone l4:teste i5:ab de5:f 8 x1:qe")
check(qgrep("fork = spoon", "stderr"))
check(parse_stdio(readfile("stdout"), 0) == readfile("output"))
check(parse_stdio(readfile("stdout"), 1) == readfile("output2"))
