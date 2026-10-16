-- verification: count instruction fetches from P2, detect error screen / BIOS return, hash the screen periodically
local n=0; local p2exec=0; local p2data=0; local err=0; local bios=0; local started=false
local PC; local OUT=os.getenv("VOUT") or "verify.txt"; local HASHEV=tonumber(os.getenv("HASHEV") or "50")
local hashes={}
local function install()
  local cpu=manager.machine.devices[":maincpu"]; local mem=cpu.spaces["program"]; PC=cpu.state["CURPC"]
  TAPX=mem:install_read_tap(0x200000,0x2FFFFF,"x",function(off,data,mask)
     local pc=PC.value
     if pc>=0x200000 and pc<0x300000 then p2exec=p2exec+1; if p2exec<=20 then print(string.format("P2EXEC frame %d pc=%06X addr=%06X",n,pc,off)) end
     else p2data=p2data+1 end
     return data end)
  TAPE=mem:install_write_tap(0x10BF10,0x10BF11,"e",function(off,data,mask) err=err+1; print(string.format("ERRSCREEN frame %d vec=%04X",n,data)); return data end)
end
local scr=manager.machine.screens[":screen"]
local function dump()
  local f=io.open(OUT,"w"); f:write(string.format("frame %d p2exec %d p2data %d err %d bios %d\n",n,p2exec,p2data,err,bios))
  for _,h in ipairs(hashes) do f:write(h.."\n") end; f:close()
end
CB=emu.register_frame_done(function()
  n=n+1
  if n==600 then install() end
  if n>800 then local pc=PC.value; if pc>=0xC00000 then bios=bios+1 end end
  if n%HASHEV==0 and n>=600 then
    local px=scr:pixels(); local h=0
    for i=1,#px,97 do h=(h*31+px:byte(i))%4294967296 end
    hashes[#hashes+1]=string.format("%d %08X",n,h)
  end
  if n%1000==0 then dump() end
end)
-- optional played games (same schedule as cov.lua)
local PLAY=tonumber(os.getenv("PLAYS") or "0")
local ioport=manager.machine.ioport
local function fld(port,name) local p=ioport.ports[port]; if p then return p.fields[name] end end
local sched={}
local function press(f,fr,len) sched[#sched+1]={f=f,s=fr,e=fr+(len or 4)} end
CB2=emu.register_frame_done(function()
  if n==601 then
    local coin=fld(":AUDIO_COIN","Coin 1"); local start=fld(":edge:joy:START","1 Player Start")
    local btnA=fld(":edge:joy:JOY1","P1 A"); local right=fld(":edge:joy:JOY1","P1 Right")
    for k=0,PLAY-1 do
      local t=3200+k*7000
      press(coin,t,4); press(coin,t+40,4); press(start,t+120,4)
      for j=1,k do press(right,t+240+j*30,6) end
      press(btnA,t+600,4); press(start,t+640,4); press(btnA,t+700,5600)
    end
  end
  for _,s in ipairs(sched) do if s.f then if n==s.s then s.f:set_value(1) elseif n==s.e then s.f:set_value(0) end end end
end)
