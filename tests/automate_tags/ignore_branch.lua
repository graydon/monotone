-- -*-lua-*-
function ignore_branch(branchname)
    if (branchname == "otherbranch") then 
       return true 
    end
    return false
end
