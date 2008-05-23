
mtn_setup()

local cwd = chdir(".")

mkdir("foo")
addfile("foo/bar", "text")

check(indir("foo",mtn("automate", "get_workspace_root")), 0, true, false)
check(qgrep("^"..cwd.."$", "stdout"))
