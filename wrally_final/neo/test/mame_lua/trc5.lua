local m=manager.machine local f=0 local gf=0 local sp=m.devices[":maincpu"].spaces["program"]
local S=m.ioport.ports[":ctrl1:joy:START_SELECT"].fields local J=m.ioport.ports[":ctrl1:joy:JOY"].fields
local G=tonumber(os.getenv("G") or "6025") local st=nil
TG=sp:install_write_tap(0x10c1a6,0x10c1a7,"g",function(o,d,mk) if d==0 then gf=gf+1
  S["P1 Start"]:set_value((gf>=4100 and gf<5000 and gf%300<20) and 1 or 0)
  S["P1 Select"]:set_value((gf>=4000 and gf<5000 and gf%300>=200 and gf%300<210) and 1 or 0)
  J["P1 A"]:set_value((gf>5200 and gf%600<560) and 1 or 0)
  J["P1 Left"]:set_value((gf>5200 and gf%240<30) and 1 or 0) end end)
emu.register_frame_done(function() f=f+1
 if gf>=G and not st then st=f+4 end
 if st and f==st then m.debugger:command("trace /tmp/claude-0/s/tr_race.txt,:maincpu,noloop") end
 if st and f==st+(tonumber(os.getenv("NF") or "3")) then m.debugger:command("trace off,:maincpu"); m:exit() end end)
