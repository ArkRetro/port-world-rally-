local R=tonumber(os.getenv("RAL") or "0") local SG=tonumber(os.getenv("STG") or "0")
return function(gf,sp,P) local t={start=(gf>=4100 and gf<5000 and gf%300<20), coin=(gf>=4000 and gf<5000 and gf%300>=200 and gf%300<210),
  acc=(gf>5200 and gf%600<560), left=(gf>5200 and gf%240<30)}
  local b=P and 0x100000 or 0xfe0000
  if gf>=5300 and gf<=5401 then sp:write_u16(b+0xf378,R) sp:write_u16(b+0xf37a,SG) end
  if gf>5700 then sp:write_u16(b+0xc1aa, 0) end
  return t end
