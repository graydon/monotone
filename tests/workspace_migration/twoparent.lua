---- If you change this code, you will have to regenerate all of the
---- "twoparent-N" workspaces.

return {
   min_format = 2,
   creator =
      function ()
	 check(mtn("setup", "-b", "twoparent-branch", "twoparent-current"))
	 chdir("twoparent-current")
	 addfile("testfile", "ancestor\nancestor")
	 addfile("attrfile", "this file has attributes")
	 commit()
	 anc = base_revision()

	 writefile("testfile", "left\nancestor")
	 check(mtn("attr", "set", "attrfile", "my other car", "made of meat"),
	       0, nil, nil)
	 commit()
	 left = base_revision()

	 revert_to(anc)
	 remove("_MTN.old")
	 writefile("testfile", "ancestor\nright")
	 commit()
	 right = base_revision()

	 check(mtn("merge_into_workspace", left), 0, false, false)
	 chdir("..")
      end,
   checker = function (dir, refdir) end
}
