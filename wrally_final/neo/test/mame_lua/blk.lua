local m=manager.machine local f=0 local gf=0 local sp=m.devices[":maincpu"].spaces["program"]
local S=m.ioport.ports[":ctrl1:joy:START_SELECT"].fields local J=m.ioport.ports[":ctrl1:joy:JOY"].fields
local fb=0 local out={}
TG=sp:install_write_tap(0x10c1a6,0x10c1a7,"g",function(o,d,mk) if d==0 then gf=gf+1
  S["P1 Start"]:set_value((gf>=4100 and gf<5000 and gf%300<20) and 1 or 0)
  S["P1 Select"]:set_value((gf>=4000 and gf<5000 and gf%300>=200 and gf%300<210) and 1 or 0)
  J["P1 A"]:set_value((gf>5200 and gf%600<560) and 1 or 0)
  J["P1 Left"]:set_value((gf>5200 and gf%240<30) and 1 or 0)
  if gf%50==0 and gf>=5600 then out[#out+1]=gf..":"..(f-fb) fb=f end
  if gf>=9000 then print(table.concat(out," ")) m:exit() end end end)
emu.register_frame_done(function() f=f+1 end)
