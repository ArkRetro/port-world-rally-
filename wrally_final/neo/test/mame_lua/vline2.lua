local m=manager.machine local cpu=m.devices[":maincpu"] local sp=cpu.spaces["program"] local n=0 local tf=0
local S=m.ioport.ports[":ctrl1:joy:START_SELECT"].fields local J=m.ioport.ports[":ctrl1:joy:JOY"].fields
local H={}
local N0=tonumber(os.getenv("N0") or "1000")
local function ln() return math.floor((m.time:as_double()-tf)*59.185606*264) end
T1=sp:install_write_tap(0x3c000c,0x3c000d,"ack",function(o,d,mk) if n>=N0 then local k=string.format("ACK%d %03d",d,ln()//8*8) H[k]=(H[k] or 0)+1 end end)
T2=sp:install_write_tap(0x400000,0x401fff,"pw",function(o,d,mk) if n>=N0 then local k=string.format("PW %06x %03d",cpu.state["CURPC"].value,ln()//8*8) H[k]=(H[k] or 0)+1 end end)
emu.register_frame_done(function() n=n+1 tf=m.time:as_double()
 local c = (n>=1200 and n<1206) local s=(n>=1300 and n<1306) or (n>=1500 and n<1506) or (n>=1700 and n<1706)
 S["P1 Select"]:set_value(c and 1 or 0) S["P1 Start"]:set_value(s and 1 or 0)
 if n>tonumber(os.getenv("NF") or "3000") then local ks={} for k,_ in pairs(H) do ks[#ks+1]=k end table.sort(ks) for _,k in ipairs(ks) do print("H",k,H[k]) end m:exit() end end)
