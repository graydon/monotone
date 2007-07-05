-- include this in your monotonerc file to gain the extra commands.

alias_command("annotate", "blame")
alias_command("annotate", "praise")

function pmup(...)
    mtn_automate("get_option", "branch")	-- make sure we have a valid workspace
    mtn_automate("pull")
    mtn_automate("merge")
    mtn_automate("update")
end

register_command("pup", "Pull, merge and update a workspace",
      "This command approximates the update command of a centralised revision control system.  " ..
      "It first contacts the server to gather new revisions, then merges multiple local heads " ..
      "(if any), and then it updates the workspace.", "pmup")

function cpm(...)
    mtn_automate("get_option", "branch")	-- make sure we have a valid workspace
    mtn_automate("commit")
    mtn_automate("pull")
    heads = mtn_automate("heads")
    words = 0
    for word in string.gfind(heads, "[^%s]+") do words=words+1 end
    if words == 1 then
        mtn_automate("push")
    else
        mtn_automate("merge")
        print("Workspace contents will not be pushed to the server.")
        print("Please check that merge was successful then push changes.")
    end
end

register_command("cpm", "Commit, pull, merge and push a workspace",
      "This command approximates the commit command of a centralised revision control system.  " ..
      "It first commits your work to the local repository, then contacts the server to gather " ..
      "new revisions.  If there is a single head at this point, then the local changes are pushed " ..
      "to the server.  If there are multiple heads then they are merged, and the user is asked " ..
      "to check things still work before pushing the changes.", "cpm")


