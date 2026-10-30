local m=manager.machine local gf=0 local sp=m.devices[":maincpu"].spaces["program"]
local P=os.getenv("PORT")=="1"
local GS={} for x in string.gmatch(os.getenv("GS"),"%d+") do GS[tonumber(x)]=true end
local last=tonumber(os.getenv("GL"))
local A=P and 0x10c1a6 or 0xfec1a6 local want=false
if P then
 local S=m.ioport.ports[":ctrl1:joy:START_SELECT"].fields local J=m.ioport.ports[":ctrl1:joy:JOY"].fields
 TG=sp:install_write_tap(A,A+1,"g",function(o,d,mk) if d==0 then gf=gf+1 if GS[gf] then want=true end
  S["P1 Start"]:set_value((gf>=4100 and gf<5000 and gf%300<20) and 1 or 0)
  S["P1 Select"]:set_value((gf>=4000 and gf<5000 and gf%300>=200 and gf%300<210) and 1 or 0)
  J["P1 A"]:set_value((gf>5200 and gf%600<560) and 1 or 0)
  J["P1 Left"]:set_value((gf>5200 and gf%240<30) and 1 or 0) end end)
else
 local S=m.ioport.ports[":P1_P2"].fields
 TG=sp:install_write_tap(A,A+1,"g",function(o,d,mk) if d==0 then gf=gf+1 if GS[gf] then want=true end
  S["1 Player Start"]:set_value((gf>=4100 and gf<5000 and gf%300<20) and 1 or 0)
  S["Coin 1"]:set_value((gf>=4000 and gf<5000 and gf%300>=200 and gf%300<210) and 1 or 0)
  S["P1 Button 1"]:set_value((gf>5200 and gf%600<560) and 1 or 0)
  S["P1 Left"]:set_value((gf>5200 and gf%240<30) and 1 or 0) end end)
end
emu.register_frame_done(function() if want then want=false m.video:snapshot() end if gf>last then m:exit() end end)
