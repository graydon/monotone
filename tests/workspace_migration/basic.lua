---- If you change this code, you will have to regenerate all of the
---- "basic-N" workspaces.

return {
   min_format = 1,
   creator =
      function ()
	 check(mtn("setup", "-b", "basicbranch", "basic-current"))
	 chdir("basic-current")
	 addfile("testfile1", "blah blah\n")
	 addfile("testfile2", "asdfas dfsa\n")
	 check(mtn("attr", "set", "testfile1", "test:attr", "fooooo"),
	       0, false, false)

	 commit("basicbranch")

	 -- make some edits to the files
	 writefile("testfile1", "new stuff\n")
	 writefile("testfile2", "more new stuff\n")
	 -- and some tree rearrangement stuff too
	 check(mtn("rename", "testfile2", "renamed-testfile2"),
	       0, false, false)
	 check(mtn("attr", "set", "renamed-testfile2", "test:attr2", "asdf"),
	       0, false, false)
	 check(mtn("attr", "drop", "testfile1", "test:attr"),
	       0, false, false)
	 mkdir("newdir")
	 writefile("newdir/file3", "twas mimsy and the borogroves\n")
	 check(mtn("add", "newdir", "newdir/file3"), 0, false, false)
	 chdir("..")
      end,
   checker = function (dir, refdir) end
}
