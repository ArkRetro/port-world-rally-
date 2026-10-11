local cpu = manager.machine.devices[":maincpu"]
local mem = cpu.spaces["program"]
local last = {rom=nil, bank=nil}
local st = {bank_n=0, bank_min=math.huge, rom_n=0, rom_min=math.huge, rom_min_pc=0, banked_n=0, fixed_n=0}
local function cyc() return cpu.state["CURCYCLES"] and cpu.state["CURCYCLES"].value or 0 end
local t0 = manager.machine.time
local function now() return (manager.machine.time - t0):as_double() end
-- tap: reads in fixed rom (0-0xFFFFF) and banked rom (0x200000-0x2FFFFF)
local t1 = mem:install_read_tap(0x200000, 0x2fefff, "banked", function(off, data, mask)
  st.banked_n = st.banked_n + 1
  local n = now()
  if last.rom then local d = n - last.rom; if d > 0 and d < st.rom_min then st.rom_min = d; st.rom_min_pc = cpu.state["CURPC"].value end end
  last.rom = n
  return data
end)
local t2 = mem:install_write_tap(0x2ffff0, 0x2fffff, "bank", function(off, data, mask)
  st.bank_n = st.bank_n + 1
  local n = now()
  if last.bank then local d = n - last.bank; if d > 0 and d < st.bank_min then st.bank_min = d end end
  last.bank = n
  if st.bank_n <= 40 then print(string.format("BANKSW t=%.6f pc=%06x off=%x data=%x", n, cpu.state["CURPC"].value, off, data)) end
end)
emu.add_machine_stop_notifier(function()
  print(string.format("RESULT banked_reads=%d min_gap_between_banked_reads=%.9fs (pc=%06x) bank_switches=%d min_gap_bankswitch=%.9fs", st.banked_n, st.rom_min, st.rom_min_pc, st.bank_n, st.bank_min))
end)
local f = 0
emu.register_frame_done(function()
  f = f + 1
  if f % 600 == 0 then
    print(string.format("FRAME %d pc=%06x banked_reads=%d bank_sw=%d min_bank_gap=%.9f", f, cpu.state["CURPC"].value, st.banked_n, st.bank_n, st.bank_min))
  end
end)
-- input driver: coin, start, then mash A / hold up+right
local io = manager.machine.ioport.ports
local function fld(tag,name) return io[tag].fields[name] end
local coin = fld(":AUDIO_COIN","Coin 1")
local start = fld(":edge:joy:START","1 Player Start")
local A = fld(":edge:joy:JOY1","P1 A")
local up = fld(":edge:joy:JOY1","P1 Up")
local right = fld(":edge:joy:JOY1","P1 Right")
local fr = 0
emu.register_frame(function()
  fr = fr + 1
  local m = fr % 120
  coin:set_value((fr>200 and fr<210) and 1 or 0)
  start:set_value((fr>300 and fr%240<6) and 1 or 0)
  A:set_value((fr>300 and m<8) and 1 or 0)
  up:set_value((fr>600 and fr%400<300) and 1 or 0)
  right:set_value((fr>600 and fr%800<200) and 1 or 0)
end)
