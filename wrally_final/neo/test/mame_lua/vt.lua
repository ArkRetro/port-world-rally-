local m=manager.machine local f=0 local gf=0 local cpu=m.devices[":maincpu"] local sp=cpu.spaces["program"]
local S=m.ioport.ports[":ctrl1:joy:START_SELECT"].fields local J=m.ioport.ports[":ctrl1:joy:JOY"].fields
TG=sp:install_write_tap(0x10c1a6,0x10c1a7,"g",function(o,d,mk) if d==0 then gf=gf+1
  S["P1 Start"]:set_value((gf>=4100 and gf<5000 and gf%300<20) and 1 or 0)
  S["P1 Select"]:set_value((gf>=4000 and gf<5000 and gf%300>=200 and gf%300<210) and 1 or 0)
  J["P1 A"]:set_value((gf>5200 and gf%600<560) and 1 or 0)
  J["P1 Left"]:set_value((gf>5200 and gf%240<30) and 1 or 0) end end)
local lastw=-1 local V={} local CLK=12000000
TV=sp:install_write_tap(0x3c0000,0x3c0003,"v",function(o,d,mk)
  if f<300 then return end
  local t=m.time:as_double()*CLK local pc=cpu.state["CURPC"].value
  if lastw>=0 then local dt=t-lastw
    local kind=(o==0x3c0002) and "dd" or "da"
    local lim=(o==0x3c0002) and 13 or 16
    if dt<lim then local k=string.format("%s %06x",kind,pc) local e=V[k] if not e then V[k]={1,dt} else e[1]=e[1]+1 if dt<e[2] then e[2]=dt end end end end
  if o==0x3c0002 then lastw=t else lastw=-1 end end)
emu.register_frame_done(function() f=f+1 if gf>=tonumber(os.getenv("GE") or "7000") then
  for k,v in pairs(V) do print(k,v[1],string.format("min %.1f",v[2])) end m:exit() end end)
