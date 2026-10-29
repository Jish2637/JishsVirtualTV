local callbacks, values, writes, logs, sends = {}, {}, {}, {}, {}
local active, world, send_ok, write_ok = true, "world-a", true, true
local outgoing = ""
reskate = {
 storage = {get=function(key) return values[key] end, set=function(key,value) values[key]=value end},
 files = {read=function(key) assert(key=="sync_outbox.txt","wrong outbox");return outgoing end,write=function(key,value) if not write_ok then return false end writes[key]=value;return true end},
 session = {
  active=function() return active end,
  me=function() return {id="42",name="Me"} end,
  host=function() return {id="42",name="Me"} end,
  world=function() return {world=world} end,
  players=function() return {{id="42",name="Me"},{id="11",name="Alice"},{id="11",name="Duplicate"},{id="22",name="Bob"}} end
 },
 mods={read_content=function() error("The watched plugin package must never carry live sync commands") end},
 net={send=function(target,data) if send_ok then table.insert(sends,{target,data}) end return send_ok end},
 console={print=function() error("Bridge emitted console traffic") end},
 log=function(message) table.insert(logs,message) end,
 on=function(name,callback) callbacks[name]=callback end
}
require=function() return function() end end
function verify_bridge()
 callbacks.load()
 assert(writes["bridge_state.txt"]:find("VTVB3|1|1|42|1|2|1|2|0",1,true),"snapshot header")
 assert(writes["bridge_state.txt"]:find("11|416C696365",1,true),"name encoding")
 assert(#logs==0,"idle logging")
 outgoing="100|0|56545331\n"
 callbacks.tick(1000)
 assert(#sends==1 and sends[1][1]=="all" and sends[1][2]=="VTS1","send")
 assert(writes["bridge_state.txt"]:find("|100|776F726C642D61\n",1,true),"private ack")
 for i=1,20 do callbacks.tick(1000) end
 assert(#sends==1 and #logs==0,"duplicate resend or idle spam")
 outgoing="101|11|56545331\n";send_ok=false;callbacks.tick(1000);assert(#sends==1)
 send_ok=true;callbacks.tick(1000);assert(#sends==2 and sends[2][1]=="11","retry")
 callbacks.message({id="11"},"VTS1private")
 assert(values.vtv_inbox:find("|11|",1,true) and not values.vtv_inbox:find("private",1,true),"private inbox")
 callbacks.message({id="11"},"not-a-tv-packet");assert(#logs==0)
 world="world-b";callbacks.tick(1000)
 assert(values.vtv_inbox=="" and writes["bridge_state.txt"]:find("|3|101|776F726C642D62\n",1,true),"world/session cleanup")
 write_ok=false;callbacks.tick(1000);callbacks.tick(1000);assert(#logs==1,"repeated failure spam")
 write_ok=true;callbacks.tick(1000);assert(#logs==2,"recovery event missing")
 reskate.session.players=function()
  local roster={{id="42",name="Me"}}
  for i=1,249 do roster[#roster+1]={id=tostring(i+1000),name=string.rep("n",128)} end
  return roster
 end
 callbacks.tick(1000)
 assert(#writes["bridge_state.txt"]<65536 and writes["bridge_state.txt"]:find("|42|1|249|1|",1,true),"full server roster")
 active=false;callbacks.tick(1000);assert(writes["bridge_state.txt"]:find("|0|0|0|0|",1,true),"offline state")
 callbacks.unload();assert(writes["bridge_state.txt"]=="","unload")
 print("Bridge Luau: quiet idle, roster, send/retry, private ACK/inbox, world reset, failure/recovery passed")
end
