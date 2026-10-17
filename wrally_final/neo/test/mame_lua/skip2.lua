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
local last=-1 local H={} local nv=0 local seq={}
emu.register_frame_done(function()
 if gf>=5600 then nv=nv+1 local v=sp:read_u16(tonumber(os.getenv("DS"))) if last>=0 then local d=(v-last)&0xffff H[d]=(H[d] or 0)+1 if d==0 then seq[#seq+1]=gf end end last=v end
 if gf>=9000 then for k,n in pairs(H) do print("D",k,n) end print("vblanks",nv) print(table.concat(seq," ")) m:exit() end end)
