-- comprueba en cada cuadro del juego que todas las columnas de mapa a la vista se ven: ranura visible,
-- en su x, con su columna (tcol) y, en la capa 0, donde la columna tiene tiles no vacios en las filas a la vista
local m=manager.machine local cpu=m.devices[":maincpu"] local sp=cpu.spaces["program"] local gf=0
local hook=dofile("/tmp/claude-0/s/inp_"..(os.getenv("INP") or "rs")..".lua")
local S=m.ioport.ports[":ctrl1:joy:START_SELECT"].fields local J=m.ioport.ports[":ctrl1:joy:JOY"].fields
local F={start=S["P1 Start"],coin=S["P1 Select"],acc=J["P1 A"],left=J["P1 Left"],right=J["P1 Right"],down=J["P1 Down"]}
local TC=tonumber(os.getenv('TC')) local TE=tonumber(os.getenv('TE')) local CV=tonumber(os.getenv('CV'))
local G0=tonumber(os.getenv('G0') or "5700") local G1=tonumber(os.getenv('G1') or "11000")
local NCOL,NSLOT,TW,CROPX,CROPY=26,32,13,8,20 local CONTENT=os.getenv('CONTENT')=='1'
local function SX(u) u=(u+32768)%65536-32768 return ((u*13)>>4)+10 end
local function rd(a) sp:write_u16(0x3c0000,a) return sp:read_u16(0x3c0002) end
local nbad=0 local nchk=0
local function check()
  sp:write_u16(0x3c0004,1)
  local out={}
  for L=0,1 do
    local sx,sy
    if L==0 then sx=(sp:read_u16(CV+2)+4)&0xffff sy=sp:read_u16(CV) else sx=sp:read_u16(CV+6) sy=sp:read_u16(CV+4) end
    local x0=(sx+CROPX-16)&1023 local c0=x0>>4 local fx=x0&15
    local Y=(CROPY+sy)&511 local r0=Y>>4
    local xb=SX(-fx-16)&0xffff
    local base=(L==1) and 1 or (1+NSLOT)
    for k=0,NCOL-1 do
      local col=(c0+k)&63 local slot=col&31 local spr=base+slot
      local h=rd(0x8200+spr)&0x3f local x=rd(0x8400+spr)>>7
      local tc=sp:read_u8(TC+L*NSLOT+slot)
      local need=true
      if L==0 then need=false
        for r=0,((Y+223)>>4)-r0 do local row=(r0+r)&31 local w0=sp:read_u16(0x100000+(row*64+col)*4)&0x3fff
          local te=(sp:read_u8(TE+(w0>>3))>>(w0&7))&1 if te==0 then need=true break end end end
      local ex=(xb+((slot-(c0&31))&31)*TW)&511
      if h>0 and tc==col and CONTENT then
        local nbad=0 local firstr=-1
        for r=0,((Y+223)>>4)-r0 do local row=(r0+r)&31 local gv=0x100000+L*0x2000+(row*64+col)*4
          local w0=sp:read_u16(gv)&0x3fff local d2=sp:read_u16(gv+2) local e1=((16+(d2&0x1f))<<8)|((d2>>6)&3)
          local v0=rd(spr*64+row*2) local v1=rd(spr*64+row*2+1)
          if v0~=w0 or v1~=e1 then nbad=nbad+1 if firstr<0 then firstr=row end end end
        if nbad>0 then out[#out+1]=string.format("L%d k%d col%d CONTENT %d rows (r%d)",L,k,col,nbad,firstr) end
      end
      if L==0 and CONTENT then   -- capa alta de esta columna
        local hs=65+2*tonumber(os.getenv("N0MAX") or "40")+slot local hh=rd(0x8200+hs)&0x3f
        if hh>0 then
          local hx=rd(0x8400+hs)>>7
          if hx~=ex then out[#out+1]=string.format("HI k%d col%d X%d exp%d",k,col,hx,ex) end
          if tc~=col then out[#out+1]=string.format("HI k%d col%d VISIBLE tc%d",k,col,tc) end
          local nbad=0 local firstr=-1
          for r=0,((Y+223)>>4)-r0 do local row=(r0+r)&31 local gv=0x100000+(row*64+col)*4
            local w0=sp:read_u16(gv)&0x3fff local d2=sp:read_u16(gv+2)
            local e0,e1 if (d2&0x20)~=0 then e0=w0+0x4000 e1=((16+(d2&0x1f))<<8)|((d2>>6)&3) else e0=0x109e e1=0 end
            local v0=rd(hs*64+row*2) local v1=rd(hs*64+row*2+1)
            if v0~=e0 or (e0~=0x109e and v1~=e1) then nbad=nbad+1 if firstr<0 then firstr=row end end end
          if nbad>0 then out[#out+1]=string.format("HI k%d col%d CONTENT %d rows (r%d)",k,col,nbad,firstr) end
        end
      end
      if need and h==0 then out[#out+1]=string.format("L%d k%d x%d HIDDEN %s",L,k,(ex>=256) and ex-512 or ex,(tc==col) and "written" or "notwritten")
      elseif h>0 and tc~=col then out[#out+1]=string.format("L%d col%d slot%d WRONGCOL tc%d",L,col,slot,tc)
      elseif h>0 and x~=ex then out[#out+1]=string.format("L%d col%d slot%d X%d exp%d",L,col,slot,x,ex) end
    end
  end
  return out
end
local function CHECK()
  nchk=nchk+1
  -- pantalla de un solo color (fundido del juego): no cuenta
  local p0=sp:read_u16(0x400200) local same=true
  for i=1,40 do if sp:read_u16(0x400200+i*26)~=p0 then same=false break end end
  if same then return end
  local o=check()
  if #o>0 then nbad=nbad+1 if nbad<=60 then print("gf",gf,#o,table.concat(o," | ",1,math.min(#o,4))) end end
end
TG=sp:install_write_tap(0x10c1a6,0x10c1a7,"g",function(o,d,mk) if d==0 then gf=gf+1
  local st=hook(gf,sp,true) for k,f in pairs(F) do f:set_value(st[k] and 1 or 0) end
  if gf>=G0 and cpu.state["CURPC"].value<0x200000 and (cpu.state["SR"].value&0x700)==0 then CHECK() end end end)
emu.register_frame_done(function() if gf>G1 then print("checks",nchk,"bad frames",nbad) m:exit() end end)
