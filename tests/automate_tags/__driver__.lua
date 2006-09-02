-- -*-lua-*-
mtn_setup()

-- automate tags on empty db
check(mtn("automate", "tags"), 0, true, true)
check(fsize("stderr") == 0)
parsed = parse_basic_io(readfile("stdout"))
for _,l in pairs(parsed) do
   if l.name == "format_version" then fversion = l.values[1] end
end
check(fversion ~= nil)
   
-- now add something
addfile("testfile", "foo")
commit("mainbranch")
r1 = base_revision()
tag1 = "tag1"

writefile("testfile", "foo bar")
commit("otherbranch")
r2 = base_revision()
tag2 = "tag2"

-- tag
check(mtn("tag", r1, tag1), 0, false, false)
check(mtn("tag", r2, tag2), 0, false, false)

-- helper function
function get_tag(tag, ...)
   check(mtn("automate", "tags", unpack(arg)), 0, true, false)
   local parsed = parse_basic_io(readfile("stdout"))
   local ltag
   for _,l in pairs(parsed) do
      if l.name == "tag" then ltag = l.values[1] end
      if ltag == tag and l.name == "revision" then
	 return l.values[1]
      end
   end
   return nil
end

-- list all tags
check(get_tag(tag1) == r1)
check(get_tag(tag2) == r2)

-- use a branch pattern
check(get_tag(tag1, "other*") == nil)
check(get_tag(tag2, "other*") == r2)

-- now ignore a otherbranch completely
get("ignore_branch.lua")
check(get_tag(tag1, "{}*", "--rcfile=ignore_branch.lua") == r1)
check(get_tag(tag2, "other*", "--rcfile=ignore_branch.lua") == nil)
