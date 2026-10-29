-- Native menu launcher only. No Luau UI polling or gameplay action buttons.
-- All controls remain inside JishsVirtualTV.dll; the existing bridge in main.lua
-- continues to relay multiplayer sync messages.
return function(boot)
    local r=reskate
    if not (r.menu and r.menu.page and r.files and r.files.write) then return end
    local seq=0
    local last_click=-100000
    r.menu.page("VIRTUAL TV",function(ui)
        ui:heading("JISH'S VIRTUAL TV")
        ui:note("Playback, playlists, screens, speakers and presets are managed in the native VirtualTV settings window.")
        if ui:button("vtv-open-settings","Open VirtualTV Settings") then
            local now=r.time.now()
            if now-last_click>=1000 then
                last_click=now
                seq+=1
                local payload="VTVM1|"..tostring(boot).."|"..tostring(seq).."|OPEN_SETTINGS|"
                local ok,result=pcall(r.files.write,"ui_commands.txt",payload)
                if not ok or not result then r.log("VirtualTV: couldn't send open settings command") end
            end
        end
        ui:note("F8 also opens the same settings window.")
    end)
end
