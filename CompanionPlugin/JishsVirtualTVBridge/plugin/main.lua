-- VTVB3: liveness, roster and acknowledgements use a private local snapshot.
-- No heartbeat or packet payload is printed to the game log.
local last_seq = "0"
local probe, heartbeat, received, serial, epoch = 0, 0, 0, 0, 1
local inbox = {}
local session_key = ""
local state_failed = false
local boot = (reskate.storage.get("vtv_boot") or 0) + 1
reskate.storage.set("vtv_boot", boot)
reskate.storage.set("vtv_inbox", "")
local function hex(text)
    local out = {}
    for i = 1, #text do out[#out + 1] = string.format("%02X", string.byte(text, i)) end
    return table.concat(out)
end
local cached_state = nil
local function publish_state()
    if not cached_state then return end
    serial += 1
    local c = cached_state
    local data = "VTVB3|" .. boot .. "|" .. serial .. "|" .. c.id .. "|" .. c.active .. "|" .. #c.roster .. "|" .. c.hosting .. "|" .. epoch .. "|" .. last_seq .. "|" .. hex(c.world) .. "\n"
    if #c.roster > 0 then data ..= table.concat(c.roster, "\n") .. "\n" end
    data ..= "END|" .. serial .. "\n"
    local ok, result = pcall(reskate.files.write, "bridge_state.txt", data)
    if not ok or not result then
        if not state_failed then reskate.log("VirtualTV bridge state write failed; check plugin file permissions") end
        state_failed = true
    elseif state_failed then
        reskate.log("VirtualTV bridge state writes recovered"); state_failed = false
    end
end
local function beat()
    local id, active, hosting, host_id = "0", 0, 0, "0"
    local roster = {}
    if reskate.session.active() then
        active = 1
        local me = reskate.session.me()
        if me and me.id then id = tostring(me.id) end
        local host = reskate.session.host()
        if host and host.id then host_id = tostring(host.id) end
        if host and me and host.id == me.id then hosting = 1 end
        local players = reskate.session.players()
        local seen = {}
        if type(players) == "table" then
            for _, player in ipairs(players) do
                if player and player.id then
                    local peer = tostring(player.id)
                    if peer ~= id and not seen[peer] and #roster < 256 then
                        seen[peer] = true
                        local name = tostring(player.name or ("Player " .. peer))
                        name = string.sub(name, 1, 96)
                        roster[#roster + 1] = peer .. "|" .. hex(name)
                    end
                end
            end
        end
    end
    local world = reskate.session.world()
    local world_id = world and tostring(world.world or "0") or "0"
    local key = active .. "|" .. id .. "|" .. host_id .. "|" .. world_id
    if key ~= session_key then
        session_key = key; epoch += 1
        inbox = {}; received = 0
        reskate.storage.set("vtv_inbox", "")
    end
    cached_state = {id=id, active=active, roster=roster, hosting=hosting, world=world_id}
    publish_state()
end
reskate.on("load", function()
    local old = reskate.files.read("sync_outbox.txt")
    if type(old) == "string" then last_seq = string.match(old, "^(%d+)|") or "0" end
    beat()
end)
reskate.on("unload", function()
    pcall(reskate.files.write, "bridge_state.txt", "")
end)
reskate.on("tick", function(dt)
    heartbeat += dt
    if heartbeat >= 1000 then heartbeat = 0; beat() end
    probe += dt
    if probe < 50 then return end
    probe = 0
    local raw = reskate.files.read("sync_outbox.txt")
    if type(raw) ~= "string" then return end
    local seq, target, data = string.match(raw, "^(%d+)|(%d+)|([0-9A-Fa-f]+)%s*$")
    if not seq or seq == last_seq then return end
    if not reskate.session.active() or #data > 3000 or #data % 2 ~= 0 then return end
    local bytes = {}
    for i = 1, #data, 2 do bytes[#bytes + 1] = string.char(tonumber(string.sub(data, i, i + 1), 16)) end
    local ok, sent = pcall(reskate.net.send, target == "0" and "all" or target, table.concat(bytes))
    if ok and sent then last_seq = seq; publish_state() end
end)
reskate.on("message", function(from, payload)
    if type(payload) ~= "string" or #payload > 1500 or string.sub(payload, 1, 4) ~= "VTS1" or not from or not from.id then return end
    received += 1
    inbox[#inbox + 1] = tostring(boot) .. "|" .. tostring(received) .. "|" .. from.id .. "|" .. hex(payload)
    if #inbox > 16 then table.remove(inbox, 1) end
    reskate.storage.set("vtv_inbox", table.concat(inbox, ";"))
end)
local menu_loaded, menu_module = pcall(require, "tv_menu")
if menu_loaded and type(menu_module) == "function" then
    local ok, problem = pcall(menu_module, boot)
    if not ok then reskate.log("VirtualTV menu unavailable: " .. tostring(problem)) end
else
    reskate.log("VirtualTV menu module missing: " .. tostring(menu_module))
end
