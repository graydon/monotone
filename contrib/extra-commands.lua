-- WARNING: this feature is not available in the mainline yet
-- you can find it in net.venge.monotone.ws_automate

-- include this in your monotonerc file to gain the extra commands.

alias_command("annotate", "blame")
alias_command("annotate", "praise")

function pup(...)
    mtn_automate("get_option", "branch")	-- make sure we have a valid workspace
    mtn_automate("pull")
    mtn_automate("update")
end

register_command("pup", "", "Pull and update a workspace",
      "This command approximates the update command of a centralised revision control system.  " ..
      "It first contacts the server to gather new revisions and then it updates the workspace.", "pup")

function cpp(...)
    mtn_automate("get_option", "branch")	-- make sure we have a valid workspace
    mtn_automate("commit")
    mtn_automate("pull")
    local ok, heads = mtn_automate("heads")
    words = 0
    for word in string.gfind(heads, "[^%s]+") do words=words+1 end
    if words == 1 then
        mtn_automate("push")
    else
        print("There are multiple heads in your current branch.")
        print("You should run 'mtn mup' to merge the heads and update.")
        print("After you have verified the merge works, run 'mtn cpp'")
        print("again to commit and push the changes.")
    end
end

register_command("cpp", "", "Commit, pull and push a workspace",
      "This command approximates the commit command of a centralised revision control system.  " ..
      "It first commits your work to the local repository, then contacts the server to gather " ..
      "new revisions.  If there is a single head at this point, then the local changes are pushed " ..
      "to the server.", "cpp")

function mup(...)
    mtn_automate("get_option", "branch")	-- make sure we have a valid workspace
    mtn_automate("merge")
    mtn_automate("update")
end

register_command("mup", "", "Merge and update a workspace",
      "This command merges multiple heads of a branch, and then updates the current workspace" ..
      "to the resulting revision.", "mup")


