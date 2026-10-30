local m=manager.machine local gf=0 local sp=m.devices[":maincpu"].spaces["program"]
local P=os.getenv("PORT")=="1" local A=P and 0x10c1a6 or 0xfec1a6 local VR=tonumber(os.getenv("VR"))
if P then local S=m.ioport.ports[":ctrl1:joy:START_SELECT"].fields local J=m.ioport.ports[":ctrl1:joy:JOY"].fields
 TG=sp:install_write_tap(A,A+1,"g",function(o,d,mk) if d==0 then gf=gf+1
  S["P1 Start"]:set_value((gf>=4100 and gf<5000 and gf%300<20) and 1 or 0)
  S["P1 Select"]:set_value((gf>=4000 and gf<5000 and gf%300>=200 and gf%300<210) and 1 or 0)
  J["P1 A"]:set_value((gf>5200 and gf%600<560) and 1 or 0)
  J["P1 Left"]:set_value((gf>5200 and gf%240<30) and 1 or 0) end end)
else local S=m.ioport.ports[":P1_P2"].fields
 TG=sp:install_write_tap(A,A+1,"g",function(o,d,mk) if d==0 then gf=gf+1
  S["1 Player Start"]:set_value((gf>=4100 and gf<5000 and gf%300<20) and 1 or 0)
  S["Coin 1"]:set_value((gf>=4000 and gf<5000 and gf%300>=200 and gf%300<210) and 1 or 0)
  S["P1 Button 1"]:set_value((gf>5200 and gf%600<560) and 1 or 0)
  S["P1 Left"]:set_value((gf>5200 and gf%240<30) and 1 or 0) end end) end
local lx,ly=nil,nil local H={} local n=0 local jerk=0 local pd=nil
local function s(v) v=v&0x3ff if v>=512 then v=v-1024 end return v end
emu.register_frame_done(function()
 if gf>=6000 then local y=sp:read_u16(VR) local x=sp:read_u16(VR+2)
  if lx then local d=math.abs(s(x-lx))+math.abs(s(y-ly)) n=n+1 if pd and pd>=3 and (d==0 or d>=2*pd-1) then jerk=jerk+1 end H[d]=(H[d] or 0)+1 pd=d end lx,ly=x,y end
 if gf>=9000 then local t={} for k,c in pairs(H) do t[#t+1]=k..":"..c end table.sort(t) print("SC",table.concat(t," ")) print("vblanks",n,"jerks",jerk) m:exit() end end)
