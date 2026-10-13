local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local FAULT = tonumber(os.getenv("FAULT") or "0")
local fr, resets, caught, done = 0, 0, 0, false
pcall(function() emu.add_machine_reset_notifier(function() if fr>10 then resets=resets+1; print("RESET at frame "..fr) end end) end)
emu.register_frame_done(function()
  fr=fr+1
  local pc=cpu.state["CURPC"].value
  if pc>=0x1600 and pc<0x1F00 then caught=caught+1 end
  if FAULT==1 and not done and fr>=2330 and pc>=0xDAFE and pc<0xDB06 then mem:write_direct_u16(0xDB06,0x4AFC); done=true; print(string.format("FAULT injected fr=%d cnt=%x",fr,mem:read_u16(0x10f38c))) end
  if fr%600==0 then print(string.format("R fr=%d cnt=%x pc=%06x resets=%d caught_frames=%d",fr,mem:read_u16(0x10f38c),pc,resets,caught)) end
  if FAULT==1 and (fr==2420 or fr==3000) then manager.machine.video:snapshot() end
end)
