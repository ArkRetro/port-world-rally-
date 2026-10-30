local m=manager.machine local f=0 local gf=0 local cpu=m.devices[":maincpu"] local sp=cpu.spaces["program"] local st=cpu.state["CURPC"]
local S=m.ioport.ports[":ctrl1:joy:START_SELECT"].fields local J=m.ioport.ports[":ctrl1:joy:JOY"].fields
TG=sp:install_write_tap(0x10c1a6,0x10c1a7,"g",function(o,d,mk) if d==0 then gf=gf+1
  S["P1 Start"]:set_value((gf>=4100 and gf<5000 and gf%300<20) and 1 or 0)
  S["P1 Select"]:set_value((gf>=4000 and gf<5000 and gf%300>=200 and gf%300<210) and 1 or 0)
  J["P1 A"]:set_value((gf>5200 and gf%600<560) and 1 or 0)
  J["P1 Left"]:set_value((gf>5200 and gf%240<30) and 1 or 0) end end)
local worst=0 local nover=0 local nchk=0 local pend=false
local function rd(a) sp:write_u16(0x3c0000,a) return sp:read_u16(0x3c0002) end
emu.register_frame_done(function() f=f+1
 if gf>5600 and f%7==0 then pend=true end
 if pend and st.value<0x200000 and (cpu.state["SR"].value&0x700)==0 then pend=false nchk=nchk+1
   sp:write_u16(0x3c0004,1)
   local drop={0,0,0,0,0} local dl={} local cnt={} local cat={} for l=0,263 do cnt[l]=0 cat[l]={0,0,0,0,0} end
   for n=1,380 do local s3=rd(0x8200+n) local h=s3&63 if h>0 then local y=(496-(s3>>7))&511 local hh=h*16 if h==32 then hh=512 end
     for l=0,hh-1 do local yy=(y+l)&511 if yy<224 then cnt[yy]=cnt[yy]+1 local c= (n<=32) and 1 or (n<=64) and 2 or (n<=144) and 3 or (n<=176) and 4 or 5 if cnt[yy]>96 then drop[c]=drop[c]+1 dl[yy]=true end cat[yy][c]=cat[yy][c]+1 end end end end
   local mx=0 local ml=0 for l=0,223 do if cnt[l]>mx then mx=cnt[l] ml=l end end
   if mx>worst then local c=cat[ml] print(string.format('line %d tot %d L1 %d L0 %d obj0 %d hi %d obj1 %d',ml,mx,c[1],c[2],c[3],c[4],c[5])) end
   local nd=0 for k,v in pairs(dl) do nd=nd+1 end if nd>0 then print(string.format('f %d gf %d lines_over %d first %d drop L1 %d L0 %d o0 %d hi %d o1 %d',f,gf,nd,(function() local a=999 for k,v in pairs(dl) do if k<a then a=k end end return a end)(),drop[1],drop[2],drop[3],drop[4],drop[5])) end
   if mx>worst then worst=mx end if mx>96 then nover=nover+1 end end
 if gf>=9000 then print("checks",nchk,"worst line",worst,"frames over 96",nover) m:exit() end end)
