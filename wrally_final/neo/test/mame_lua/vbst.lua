local m=manager.machine local gf=0 local sp=m.devices[":maincpu"].spaces["program"] local cpu=m.devices[":maincpu"]
local A=0x10c1a6
local S=m.ioport.ports[":ctrl1:joy:START_SELECT"].fields local J=m.ioport.ports[":ctrl1:joy:JOY"].fields
TG=sp:install_write_tap(A,A+1,"g",function(o,d,mk) if d==0 then gf=gf+1
  S["P1 Start"]:set_value((gf>=4100 and gf<5000 and gf%300<20) and 1 or 0)
  S["P1 Select"]:set_value((gf>=4000 and gf<5000 and gf%300>=200 and gf%300<210) and 1 or 0)
  J["P1 A"]:set_value((gf>5200 and gf%600<560) and 1 or 0)
  J["P1 Left"]:set_value((gf>5200 and gf%240<30) and 1 or 0) end end)
local V={} for k,a in pairs({sr=0x10a70d,qc=0x10a70c,qb=0x10ab5c,cb=0x10abc0,ch=0x10a529,cv=0x10abc1,ic=0x10ac22,fb=0x10ad78}) do V[k]=a end
local H={} local n=0 local lastt=0
local function key() local s="" for _,k in ipairs({"sr","qc","qb","cb","ch","cv","ic"}) do s=s..k..sp:read_u8(V[k]).." " end return s end
local inp=false LNF=-1 NV=0
TR=sp:install_write_tap(0x10a578,0x10a579,"p1",function(o,d,mk) if gf>=5600 and gf<9000 and not inp and sp:read_u16(0x10a578)~=LNF then LNF=sp:read_u16(0x10a578) inp=true
  local ok,k=pcall(function() local scr=m.screens:at(1) return key() end) if not ok and n<3 then print("ERR",k) n=n+1 end
  H[k]=(H[k] or 0)+1 NV=NV+1 inp=false end return d end)
emu.register_frame_done(function()
 if gf>=9000 then print("NV",NV) local t={} for k,c in pairs(H) do t[#t+1]=string.format("%5d %s",c,k) end table.sort(t) print(table.concat(t,"\n")) m:exit() end end)
