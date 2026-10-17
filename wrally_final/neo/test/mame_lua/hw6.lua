-- detector de escrituras de VRAM visibles durante la imagen activa (lo que MAME no muestra)
local m=manager.machine local f=0 local gf=0 local cpu=m.devices[":maincpu"] local sp=cpu.spaces["program"]
local S=m.ioport.ports[":ctrl1:joy:START_SELECT"].fields local J=m.ioport.ports[":ctrl1:joy:JOY"].fields
TG=sp:install_write_tap(0x10c1a6,0x10c1a7,"g",function(o,d,mk) if d==0 then gf=gf+1
  S["P1 Start"]:set_value((gf>=4100 and gf<5000 and gf%300<20) and 1 or 0)
  S["P1 Select"]:set_value((gf>=4000 and gf<5000 and gf%300>=200 and gf%300<210) and 1 or 0)
  J["P1 A"]:set_value((gf>5200 and gf%600<560) and 1 or 0)
  J["P1 Left"]:set_value((gf>5200 and gf%240<30) and 1 or 0) end end)
NP=0 local V={} local addr=0 local mod=0 local tf=0 local R={} local G0=tonumber(os.getenv("G0") or "5600")
local function cls(sl) if sl<=32 then return "L1" elseif sl<=64 then return "L0" elseif sl<=144 then return "O0" elseif sl<=176 then return "HI" else return "O1" end end
local function visible(sl)
  local s3=V[0x8200+sl] or 0 local h=s3&63 if h==0 then return false end
  local x=((V[0x8400+sl] or 0)>>7)&511 if x>=448 then x=x-512 end
  return x+13>16 and x<304 end
TV=sp:install_write_tap(0x3c0000,0x3c0005,"v",function(o,d,mk)
  if o==0x3c0000 then addr=d return end
  if o==0x3c0004 then mod=d if mod>=0x8000 then mod=mod-0x10000 end return end
  local a=addr
  if gf>=G0 and f>10 then
    local line=(m.time:as_double()-tf)*59.185606*264
    if line>40.5 and line<263 and (V[a] or -1)~=d then
      local sl,kind
      if a<0x7000 then sl=a>>6 kind="SCB1" elseif a>=0x8200 and a<0x8400 then sl=a-0x8200 kind="SCB3" elseif a>=0x8400 and a<0x8600 then sl=a-0x8400 kind="SCB4" end
      if sl and sl>0 and sl<381 and (visible(sl) or kind=="SCB3") then
        local row_ok=true
        if kind=="SCB1" then local s3=V[0x8200+sl] or 0 local top=(496-(s3>>7))&511 local i=(a&63)>>1
          local y=(top+16*i)&511 row_ok=(y<224 or y>496) end
        if row_ok and kind=="SCB1" and NP<40 then local pc=cpu.state["CURPC"].value if pc>=0x00202cde and pc<0x00202dd6 then NP=NP+1 local spv=cpu.state["SP"].value print(string.format("EV gf %d line %.0f slot %d ret %08x",gf,line,sl,sp:read_u32(spv+38))) end end
        if row_ok then local k=string.format("%s %s %06x",cls(sl),kind,cpu.state["CURPC"].value) local e=R[k] if e then e[1]=e[1]+1 if line>e[2] then e[2]=line end else R[k]={1,line} end end
      end end end
  V[a]=d addr=(addr+mod)&0xFFFF end)
emu.register_frame_done(function() f=f+1 tf=m.time:as_double() if gf>=tonumber(os.getenv("GE") or "7000") then
  local ks={} for k,_ in pairs(R) do ks[#ks+1]=k end table.sort(ks,function(a,b) return R[a][1]>R[b][1] end)
  for _,k in ipairs(ks) do print("VIS",k,R[k][1],string.format("max line %.0f",R[k][2])) end print("frames",f) m:exit() end end)
