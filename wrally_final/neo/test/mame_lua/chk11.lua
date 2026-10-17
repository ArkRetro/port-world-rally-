local m=manager.machine local f=0
local cpu=m.devices[":maincpu"] local sp=cpu.spaces["program"] local st=cpu.state["CURPC"] local pend=false
local S=m.ioport.ports[":ctrl1:joy:START_SELECT"].fields
local J=m.ioport.ports[":ctrl1:joy:JOY"].fields
local TC=tonumber(os.getenv('TC')) local N=32 local C1=tonumber(os.getenv('C1')) local L0M=tonumber(os.getenv('L0M')) local TE=tonumber(os.getenv('TE'))
local prev={} local gf=0
TG=sp:install_write_tap(0x10c1a6,0x10c1a7,"g",function(o,d,mk) if d==0 then gf=gf+1 end end)
local tot=0
local function check()
 local bad={}
 for layer=0,1 do for slot=0,N-1 do local col=sp:read_u8(TC+layer*N+slot)
  if col~=0xff then for r=0,31 do local idx=r*64+col local gv=0x100000+layer*0x2000+idx*4
   local w0=sp:read_u16(gv) local d2=sp:read_u16(gv+2)
   local e0=w0&0x3fff local e1=((16+(d2&0x1f))<<8)|((d2>>6)&3)
   local base=((layer==1) and 1 or (1+N))+slot
   sp:write_u16(0x3c0004,1) sp:write_u16(0x3c0000,base*64+r*2) local v0=sp:read_u16(0x3c0002)
   sp:write_u16(0x3c0000,base*64+r*2+1) local v1=sp:read_u16(0x3c0002)
   if layer==0 then local cm=sp:read_u32(C1+col*4) local lm=sp:read_u32(L0M+col*4)
     local b=(cm>>r)&1 local want=((d2&0x20)~=0) and 1 or 0
     if b~=want then bad["c1:"..col..":"..r]="c1mask col "..col.." r "..r end
     local te=(sp:read_u8(TE+(e0>>3))>>(e0&7))&1 local lb=(lm>>r)&1
     if lb==te then bad["l0:"..col..":"..r]="l0mask col "..col.." r "..r end end
   if v0~=e0 or v1~=e1 then bad[layer..":"..slot..":"..r]=string.format("L%d s%d c%d r%d vram=%04x:%04x gv=%04x:%04x",layer,slot,col,r,v0,v1,e0,e1) end end end end end
 return bad
end
emu.register_frame_done(function() f=f+1
 S["P1 Start"]:set_value((gf>=4100 and gf<5000 and gf%300<20) and 1 or 0)
 S["P1 Select"]:set_value((gf>=4000 and gf<5000 and gf%300>=200 and gf%300<210) and 1 or 0) J["P1 Left"]:set_value((gf>5200 and gf%240<30) and 1 or 0)
 J["P1 A"]:set_value((gf>5200 and gf%600<560) and 1 or 0)
 if f%50==0 and f>1000 then pend=true end
 if pend and st.value<0x200000 and (cpu.state["SR"].value & 0x0700)==0 then pend=false local b=check()
   for k,v in pairs(b) do if prev[k] then tot=tot+1 if tot<4000 then end end end
   prev=b local n=0 for k,v in pairs(b) do n=n+1 end if n>0 then print(f,"mismatch",n) local c=0 for k,v in pairs(b) do c=c+1 if c<=6 then print(v) end end end if f==11350 or f==11450 or f==11600 then m.video:snapshot() end end
 if gf>=9000 then print("total persistent",tot) m:exit() end end)
