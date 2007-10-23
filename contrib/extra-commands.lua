-- include this in your monotonerc file to gain the extra commands.

-- Not all of the automate commands used by this lua code are committed
-- to trunk at present.  As such this should currently be treated as
-- sample code, not working commands.

if alias_command == nil then
    -- If we're using an older version of monotone that cannot handle lua commands
    -- then turn make sure we don't error.
    function alias_command(...) print("Warning: alias_command() not available in this version of Monotone.") end
end

if mtn_automate == nil or register_command == nil then
    function register_command(...) print("Warning: register_command() not available in this version of Monotone.") end
end

alias_command("annotate", "blame")
alias_command("annotate", "praise")

function net_update(...)
    result, output = mtn_automate("get_option", "branch")	-- make sure we have a valid workspace
    if not result then
        print("Error from mtn automate call to get_option: ", ouput)
        print("Do you have a vaild workspace?")
        return
    end
    result, output = mtn_automate("pull")
    if not result then
        print("Error from mtn automate call to pull: ", output)
        return
    end
    result, output = mtn_automate("update")
    if not result then
        print("Error from mtn automate call to update: ", output)
        return
    end
    -- print(output)
end

register_command("net_update", "", "Pull and update a workspace",
      "This command approximates the update command of a centralised revision control system.  " ..
      "It first contacts the server to gather new revisions and then it updates the workspace.", "net_update")

alias_command("net_update", "nup")

function net_commit(...)
    result, output = mtn_automate("get_option", "branch")	-- make sure we have a valid workspace
    if not result then
        print("Error from mtn automate call to get_option: ", output)
        print("Do you have a vaild workspace?")
        return
    end
    result, output = mtn_automate("commit")
    if not result then
        print("Error from mtn automate call to commit: ", output)
        return
    end
    print(output)
    result, output = mtn_automate("pull")
    if not result then
        print("Error from mtn automate call to pull: ", output)
        return
    end
    result, heads = mtn_automate("heads")
    if not result then
        print("Error from mtn automate call to heads: ", output)
        return
    end
    words = 0
    for word in string.gfind(heads, "[^%s]+") do words=words+1 end
    if words == 1 then
        result, output = mtn_automate("push")
        if not result then
            print("Error from mtn automate call to push: ", output)
            return
        end
    else
        print("There are multiple heads in your current branch.")
        print("You should run 'mtn merge_update' to merge the heads and update.")
        print("After you have verified the merged revision is ok, run 'mtn nci'")
        print("again to commit and push the changes.")
    end
end

register_command("net_commit", "", "Commit, pull and push a workspace",
      "This command approximates the commit command of a centralised revision control system.  " ..
      "It first commits your work to the local repository, then contacts the server to gather " ..
      "new revisions.  If there is a single head at this point, then the local changes are pushed " ..
      "to the server.", "net_commit")

alias_command("net_commit", "nci")

function merge_update(...)
    result, output = mtn_automate("get_option", "branch")	-- make sure we have a valid workspace
    if not result then
        print("Error from mtn_automate call to get_option: ", output)
        print("Do you have a vaild workspace?")
        return
    end
    result, output = mtn_automate("merge")
    if not result then
        print("Error from mtn_automate call to heads: ", output)
        return
    end
    -- print(output)
    result, output = mtn_automate("update")
    if not result then
        print("Error from mtn_automate call to heads: ", output)
        return
    end
    -- print(output)
end

register_command("merge_update", "", "Merge and update a workspace",
      "This command merges multiple heads of a branch, and then updates the current workspace " ..
      "to the resulting revision.", "merge_update")

alias_command("merge_update", "mup")
