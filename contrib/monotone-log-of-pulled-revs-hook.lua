-- This is a simple Monotone hook function that prints out information about
-- revisions received during netsync.

function note_netsync_revision_received(new_id, revision, certs, session_id)
    local date=""
    local author=""
    local changelog=""
    local branches=""
    for i,cert in pairs(certs) do 
        if cert["name"] == "date" then
            date = date .. cert["value"] .. " "
        end
        if cert["name"] == "author" then
            author = author .. cert["value"] .. " "
        end
        if cert["name"] == "branch" then
            branches = branches .. cert["value"] .. " "
        end
        if cert["name"] == "changelog" then
            changelog = changelog .. cert["value"] .. "\n"
        end
    end
    print("------------------------------------------------------------")
    print("Revision: " .. new_id)
    print("Author:   " .. author)
    print("Date:     " .. date)
    print("Branch:   " .. branches)
    print()
    print(changelog)
end

