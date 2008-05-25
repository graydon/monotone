-- mtn 0.4 had problems when checking out into the Windows root directory

skip_if(ostype ~= "Windows")

mtn_setup()

addfile("testfile", "foo")
commit()

root = os.tmpname()
check(indir(os.getenv("HOMEDRIVE").."/", mtn_outside_ws("--branch=testbranch", "checkout",  root)), 0, false, nil)
remove(root)

-- end of file

