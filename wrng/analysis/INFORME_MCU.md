# World Rally (Gaelco 1993) — Qué hace el DS5002FP (`wrdallas.bin`) y cómo reimplementarlo en 68000

Fuentes: `$LAB/work/mcu/mcu.asm` (desensamblado 8051), `$LAB/src/wrally/wrdallas.bin`, `$LAB/work/mcu/main.asm`
(68k), MAME 0.264 (`mcs51.cpp`, `gaelco_ds5002fp.cpp`, `wrally.cpp`). Verificación dinámica con taps Lua
(`tap3.lua` = 120 s con 65 s de atracción incl. demo; `tap4.lua` = 120 s con moneda+start a los 10 s y partida real;
`t2/timing.lua` = latencias). Apéndice: `scripts.txt` = los 12 guiones de etapa decodificados (validado contra la
traza: 83/83 transiciones de puntero coinciden).

Convención: `C101` = dirección tal como la usa el MCU (MOVX). El MCU accede a la RAM compartida con direcciones
`0xC000-0xFFFF`; MAME las enmascara con `0x3FFF`, así que **la dirección MCU coincide con los 16 bits bajos de la
dirección 68k**: `C101 ↔ $FEC101`, `EAA0 ↔ $FEEAA0`, `F501 ↔ $FEF501`. Los words del 68k son big-endian: el byte
`C101` es el byte bajo del word `$FEC100`.

---------------------------------------------------------------------------------------------------------------------

## 0. Resumen ejecutivo

* El DS5002FP **no es un coprocesador de física ni de rivales**. Hace tres cosas:
  1. **Firma de presencia**: cuando el 68k escribe `move.w #1,$FEF500` el MCU responde `$FEF500 = $A53E`
     ("Coprocessor OK"; si no, "Coprocessor Not Ready" y el self-test se queda en bucle).
  2. **Intérprete de un guion por etapa** (12 guiones, 12,1 KB en 0x0960-0x37FF de `wrdallas.bin`): una lista
     de *disparadores por posición del coche* que, al cumplirse, emiten eventos al 68k: señal del copiloto (flecha),
     marcador de progreso Start→Finish (24 tramos), punto de reinicio (checkpoint), inicio de carrera (parámetros
     del "coche fantasma"/referencia), cambio de color de fondo, final de etapa con bonificación de tiempo al
     marcador, y **órdenes de autopilotaje** (ir hacia un punto / orientarse a un rumbo).
  3. **Autopiloto** por frame (máquina de estados `C229`): a partir de la posición/rumbo del coche calcula un
     comando de giro (`C20D`, `C22B`, `C22D`) usando tablas atan2 y de zonas de distancia; en dos situaciones
     escribe directamente rumbo y vector de velocidad del coche (`EAB1`, `EABC`, `EAA8`, `EAAC`).
* Protocolo: una petición por frame **sólo durante la carrera** (demo y partida): 68k `move.w #1,$FEC102` (C103=1)
  al final de su lógica de frame → MCU: `C103=0, C101=0, guion(), autopiloto(), C101=1` → 68k comprueba
  `tst.w $FEC100` al principio del frame siguiente. Latencia medida 0,14-1,8 ms (media 0,63 ms); el 68k nunca llega
  a esperar (1 lectura de `$FEC100` por frame, siempre =1). **El resultado se consume en el frame siguiente.**
* El MCU no usa su SRAM como datos (0x10000-0x17FFF nunca se toca), no hay checksums, ni aleatorios, ni
  cronómetro, ni dificultad, ni rivales. Sólo RAM interna ($30-$52) como scratch.
* Además: interrupción de fallo de alimentación (PFI) → escribe `EAD5=1` y se para; el 68k (`$318C`
  `cmpi.w #1,$FEEAD4` → `$E68A`) muestra "WARNING / Tension baja". En el port basta con dejar `$FEEAD4=0`.
* Para el port: implementar en 68k (a) `$FEF500=$A53E` inmediato, (b) el intérprete del guion + autopiloto como
  una subrutina llamada justo donde el juego escribe `C103=1` (`$3634`), dejando `C103=0, C101=1`. Los 12 guiones y
  las 7 tablas se copian tal cual de `wrdallas.bin` (offsets abajo).

---------------------------------------------------------------------------------------------------------------------

## 1. Configuración del DS5002FP y mapa de `wrdallas.bin`

```
0100: SP=$70 ; MCON=$88 ; RPCTL=$20 ; CRCR=$80 ; PCON|=8
```
MCON=$88 → PA=8 (partición en 0x8000), RG1=1; RPCTL=$20 → EXBS=1 (bus expandido), RG0=0 ⇒ rango 0x7FFF. Con
PA ≥ rango **ninguna dirección MOVX cae en la SRAM propia**: todas van al bus expandido = RAM compartida
(MAME: `external_ram_iaddr` no suma 0x10000). Confirmado por la traza: 0 accesos a 0x10000+.

| Zona `wrdallas.bin` | Contenido |
|---|---|
| 0000-003F | vectores (todos `ljmp $0100`; INT 0x2B = PFI → `$0040`) |
| 0040-007A | ISR PFI: espera a que PCON.5 se borre (bucle con DPTR como contador), `EAD5=1`, `TA` unlock, `PCON|=2` (STOP) |
| 0100-018D | arranque + bucle principal |
| 018E-0617 | AUTOPILOTO (máquina de estados `C229`) |
| 0618-079B | VELOCIDAD (vector velocidad desde rumbo) |
| 079C-07EB | DIV32x16 |
| 07EC-0813 | tabla ZONA (4×3 longs; la entrada 3 solapa con la siguiente tabla) |
| 0810-08BF | 4 tablas ATAN de 11×4 bytes (cuadrantes) |
| 08C0-095F | tabla SENO/COSENO 40×(int16,int16) |
| 0960-37FF | 12 guiones de etapa (bases en 4044) |
| 3800-3823 | basura / relleno (`ljmp $0306` nunca usado) |
| 3824-3FA3 | INTÉRPRETE DE GUION + comandos |
| 3FA4-3FFF | tabla FLAG_SEÑAL (byte por tipo de señal) |
| 4044-405B | 12 punteros big-endian a los guiones: 0960 0D90 11A6 157C 193E 1D82 2160 2524 28B4 2D10 30D6 3466 |
| 405C-405D | constante 0x2EE0 (12000) |
| 405E-0x4089 | tabla MULT: 11 pares (int16,int16) |
| resto | ceros |

RAM interna usada: $30-$4F scratch 32 bits, $51:$52 puntero del barrido ocioso, $50=0x88, pila desde $70.

---------------------------------------------------------------------------------------------------------------------

## 2. Protocolo con el 68k

### 2.1 Arranque del MCU (0x0130-0x013E)
```
[0000] = $88          ; escribe $88 en $FEC000 (antes de que el 68k borre la RAM; sin efecto)
[EAD5] = 0            ; flag "fallo de alimentación" a 0  ($FEEAD4.w = 0)
```

### 2.2 Bucle principal (0x013F-0x018C), exacto
```
loop:
  if [F501] == 1 { [F500] = $A5 ; [F501] = $3E }           ; firma: 68k escribe word 1 en $FEF500, lee $A53E
  ; --- barrido ocioso (sin efecto funcional) ---
  dptr = ($51:$52) + 1
  if dpl == $9A { dph = [C267] ; dpl = $9A }               ; C267 = byte bajo de $FEC266 (PRNG del 68k en $5E16)
  dph &= $3F ; ($51:$52) = dptr ; lectura dummy [dptr]      ; 16 KB leídos secuencialmente, ~34000 lecturas/s
  ; --- petición ---
  if [C103] != 1 goto loop
  [C103] = 0
  [C101] = 0
  lcall GUION      ($3824)
  acall AUTOPILOTO ($018E)
  [C101] = 1
  goto loop
```
Sondeo de `C103`/`F501`: ~34 000 veces/s cada uno (≈570 sondeos por frame medidos).

### 2.3 Lado 68k
| PC 68k | Código | Significado |
|---|---|---|
| `$249C`, `$2812` | `move.w #1,$FEF500` | self-test: pide la firma |
| `$2800` | `cmpi.w #$A53E,$FEF500` → texto `$ED8AE` "Coprocessor OK" / `$ED8C8` "Coprocessor Not Ready" | |
| `$2832` | `cmpi.w #$A53E,$FEF500 ; bne $2864` → `$2864` espera botón y vuelve a `$2770` (self-test en bucle) | sin MCU no arranca |
| `$318C` | `cmpi.w #1,$FEEAD4 ; beq → jmp $E68A` | "WARNING Tension baja" (EAD5=1 lo pone la ISR PFI del MCU) |
| `$8DF2` | `move.w #1,$FEC100` (C101=1) | antes de entrar en el bucle de carrera (llamado desde `$32A0`, `$8280/$82BE/$8316`) para que la 1ª espera pase |
| `$32B8` | bucle de carrera: `tst.w $FEC1A6 / beq` (VBlank), … | |
| `$33F0` | `tst.w $FEC100 ; beq $33F0` | espera C101≠0 (respuesta del frame anterior) |
| `$3634` | `move.w #1,$FEC102` (C103=1) | **petición**, una por frame, tras toda la lógica del frame |
| `$3644-$3662` | copia `C244→C248`, `C252→C258`, swap de buffers, `bra $32B8` | |
| `$8354` / `$834C`,`$86DC` | `ori.w #1,$FEC1C6` / `andi.w #$400,$FEC1C6` | C1C7=1 desactiva el guion / C1C7=0 lo activa (el MCU pone 2 en FINISH) |
| `$923C` | `clr.w $FEC16C` | puntero de guion = 0 al empezar etapa |
| `$925E` | `move.w $FEC17A,$FEC16C` | en demo (bit0 de `$FEC172`) empieza en el checkpoint guardado |
| `$AD82-$ADB2` | `C16C = C17A = EB20 ; EAA0 = EB1C ; EAA4 = EB1E ; C228=0 ; C271=1` | reinicio en el último checkpoint |
| `$3DF6` | `move.w D0,$FEC16E` (D0 = `$FEC198`-1) | C16F = índice de etapa 0..11 |
| `$5E00` | `C244.l = C268.l ; C252.l = C26C.l` | restaura lo que el MCU copió en FINISH |
| `$3C30` | `clr.w $FEC104` | borra C105 (único uso; el 68k nunca lo lee: comando 9 es inerte en este set) |

Fuera del bucle de carrera (menús, mapa, self-test) **no hay peticiones**; en la traza las peticiones empiezan al
iniciar la carrera (demo a los 53 s; partida a los 34 s) y se mantienen 1/frame.

### 2.4 Variables compartidas (todas)
`E`=entrada que el MCU lee (la escribe el 68k), `S`=salida que escribe el MCU, `P`=privada del MCU (el 68k no la
toca salvo donde se indica). Tamaños: b=byte, w=word BE, l=long BE.

| MCU | 68k | Tipo | Dir | Significado |
|---|---|---|---|---|
| 0000 | $FEC000 | b | S | $88 al arrancar (irrelevante) |
| C101 | $FEC100.w | b | S | 0 = procesando, 1 = listo. 68k lo pone a 1 en `$8DF2` |
| C103 | $FEC102.w | b | E/S | 1 = petición (68k `$3634`); el MCU lo borra |
| C105 | $FEC104.w | b | S | =1 en comando 9 (68k sólo lo borra en `$3C30`) |
| C10E-C13D | $FEC10E | 12×l | S | tabla de 12 colores (copiada por el 68k desde ROM `$E41F2` en `$9480`; leída en `$B000` para rellenos de 2×384 longs). Comando B parchea una entrada |
| C149 / C14B | $FEC148.w / $FEC14A.w | b | S | C149=1 "nuevo valor", C14B = tramo del marcador de progreso 1..24 (68k `$3D06`) |
| C16C:C16D | $FEC16C.w | w | E/S | offset del guion dentro de la etapa (68k `$923C`,`$925E`,`$AD82`; MCU lo avanza) |
| C16F | $FEC16E.w | b | E | índice de etapa 0..11 (`$3DF6`) |
| C1A1 | $FEC1A0.w | b | S | p0 del comando B (0/1, "luz/niebla") |
| C1AA:C1AB | $FEC1AA.w | w | E | tiempo de etapa transcurrido (68k `$307A` lo incrementa cada frame; unidad ≈ 1/60 s·2…; `$3C3C` lo divide por 150 para segundos de pantalla) |
| C1B3 | $FEC1B2.w | b | S | =0 en FINISH |
| C1C7 | $FEC1C6.w | b | E/S | 0 = guion activo; 1 (68k) = parado; 2 (MCU) = etapa terminada |
| C1CA-C1CD | $FEC1CA.l | l | E/S | **marcador (score)**; FINISH suma la bonificación |
| C1FC | $FEC1FC.b | b | E | byte de entrada P1 preparado por el 68k (`$3430` demo grabada, `$58C2-$5A10` real): bit0/bit1 = giro (0 = activo), bits 2/3 = joystick, 4 = marcha, 5 = acelerador, 6/7 = monedas (activo bajo) |
| C20D | $FEC20C.w | b | S | **velocidad de giro ordenada** (0,1,2,3,$0A o copia de C20F); el 68k la mete en el nibble bajo de `$FEC224` (`$34EA`,`$560A`) y en `$61EC/$6364` resta/suma ese nibble al ángulo `EABC` cada frame |
| C20F | $FEC20E.w | b | E | velocidad de giro base del coche (68k `$8D06-$8DC0`: 9,$A,$B,$C según coche/estado) |
| C215 | $FEC214.w | b | P | sentido (0/1) del objetivo actual (bit7 de p0 en GOTO_PT) |
| C217 | $FEC216.w | b | S/E | MCU lo pone a 0 en SETDIR; 68k a 1 en `$56E6/$574E` |
| C218:C219, C21A:C21B | $FEC218.w, $FEC21A.w | w,w | P | punto objetivo (x, y) de GOTO_PT |
| C21D | $FEC21C.w | b | P | índice de tabla ZONA (p0&3 de GOTO_PT) |
| C229 | $FEC228.w | b | S | estado del autopiloto: 0 inactivo, 1 "ir a punto", 2 "orientar a rumbo" (68k lo lee en ≥20 sitios: `$34D6`,`$569A`,`$624E`…; lo borra en `$9234`,`$ADBA`) |
| C22B | $FEC22A.w | b | S | 0 nada; 1 = "ordenar giro C20D"; 2 = "ordenar giro C20D y seguir control normal" (68k `$55F2-$5630`) |
| C22D | $FEC22C.w | b | S | 1 = bloquear dirección del jugador (68k `$34F6`: `C1FC|=$0F`, `clr C202`, `C224 &= $FEF858`) |
| C22F | $FEC22E.w | b | S | rumbo objetivo de SETDIR (0..39) (68k lo lee `$3850`,`$6C2E`,`$80C0`,`$B0EA`) |
| C231 | $FEC230.w | b | P | contador 20→0 del estado 2 |
| C233 | $FEC232.w | b | S | rumbo final al terminar el estado 2 (68k `$50AA`,`$54AA`,`$558E`,`$578C`,`$67C0`,`$7F6C`) |
| C244-C247, C252-C255 | $FEC244.l, $FEC252.l | l,l | E | leídos en FINISH y copiados a C268/C26C (el 68k los restaura en `$5E00`; son contadores de posición de pista) |
| C267 | $FEC266.w | b | E | PRNG del 68k; sólo para el barrido ocioso |
| C268-C26F | $FEC268.l,$FEC26C.l | l,l | S | copia de C244/C252 en FINISH |
| C271 | $FEC270.w | b | S | estado de señal del copiloto: 1 (comando 1), 3 (SIGN) (68k `$37E2`) |
| C273 | $FEC272.w | b | S | tipo de señal p0 (68k `$38AE`: código sprite = p0 − D1 + EAB0 + $191) |
| C275 / C277 | $FEC274.w / $FEC276.w | b,b | S | p2·8 / p1·8 (68k `$38F6`: posición = `$E1D74.l − $FEC274.l`) |
| C27B | $FEC27A.w | b | S | FLAG_SEÑAL[p0] (68k `$395A`, `$720A`, `$772A`) |
| EA97 | $FEEA96.w | b | S | rumbo (copia de C22F) en RACE_START |
| EA98:EA99 | $FEEA98.l (hi) | w | E | coordenada "progreso/odómetro" (eje de disparo 00, sube ~+18/frame a tope) |
| EA9C:EA9D | $FEEA9C.l (hi) | w | E | coordenada eje 80 (68k `$51C2`, `$B8D4`, `$F7EC`) |
| EAA0:EAA1 | $FEEAA0.l (hi) | w | E | **X del coche** (parte entera) |
| EAA4:EAA5 | $FEEAA4.l (hi) | w | E | **Y del coche** (parte entera; decrece al avanzar, llega a negativa al final) |
| EAA8-EAAB / EAAC-EAAF | $FEEAA8.l / $FEEAAC.l | l,l | S | vector velocidad (vx, vy) — el 68k lo calcula igual en `$6A0E` con su propia tabla en `$E0022` |
| EAB1 | $FEEAB0.w | b | E/S | **rumbo del coche 0..39** (9° por paso) |
| EABC:EABD | $FEEABC.w | w | S | ángulo fino = rumbo·30 (0..1199) |
| EAC5, EAC7 | $FEEAC4.w, $FEEAC6.w | b,b | S | =0 en RACE_START |
| EAD1 | $FEEAD0.w | b | E/S | estado del coche (68k: 1/2 en `$614C`,`$61C0`…, 0 = normal); el estado 2 termina cuando vale 0; RACE_START lo pone a 0 |
| EAD2:EAD3 | $FEEAD2.w | w | E | **velocidad escalar** del coche |
| EAD5 | $FEEAD4.w | b | S | 0 al arrancar; 1 = fallo de alimentación (PFI) |
| EADB | $FEEADA.w | b | S | rumbo (copia de C22F) en RACE_START |
| EAE9 | $FEEAE8.w | b | S | 1 en RACE_START, 0 en RACE_START2 |
| EAEB | $FEEAEA.w | b | S | 1 en RACE_START2 |
| EAED | $FEEAEC.w | b | S | p0 de RACE_START |
| EAF1 | $FEEAF0.w | b | S | p0 de RACE_START2 |
| EAF2-EAF5 | $FEEAF2.l | l | S/E | 0 en RACE_START; copiado a EB02 en RACE_START2 |
| EAF6-EAF9 | $FEEAF6.l | l | S | (MULT[i].a·p2>>8)·EAD2 en RACE_START |
| EAFA-EAFD | $FEEAFA.l | l | S | (MULT[i].b·p2>>8)·EAD2 en RACE_START2 |
| EAFF | $FEEAFE.w | b | S | 0 en RACE_START2 |
| EB00:EB01 | $FEEB00.w | w | S | EAF6 / 12000 en RACE_START2 |
| EB02-EB05 | $FEEB02.l | l | S | copia de EAF2 |
| EB09 | $FEEB08.w | b | S | 0 en RACE_START |
| EB0B / EB0D | $FEEB0A.w / $FEEB0C.w | b,b | E/S | EB0B=1 → RACE_START se salta y pone EB0D=1; RACE_START2 lo consume |
| EB1B | $FEEB1A.w | b | S | rumbo de SETDIR |
| EB1C:EB1D, EB1E:EB1F | $FEEB1C.w, $FEEB1E.w | w,w | S | **checkpoint**: x−$18, y (68k `$AD96/$ADA8` → EAA0/EAA4 al reiniciar) |
| EB20:EB21 | $FEEB20.w | w | S | offset de guion tras el checkpoint (68k `$AD82` → C16C y C17A) |
| EB4F | $FEEB4E.w | b | S | 0 en SETDIR |
| EB51 | $FEEB50.w | b | S | nº de GOTO_PT desde el último SETDIR (68k `$6A56` compara `$FEEB50.w == 1`) |
| EBD7 | $FEEBD6.w | b | S | 0 en RACE_START |
| EBDD | $FEEBDC.w | b | E | "porcentaje" (68k `$B590` =100, `$8192` −=40…): índice de MULT = (EBDD+4)/10 |
| EBE5 / EBE7 | $FEEBE4.w / $FEEBE6.w | b,b | S | 1 / $87 en RACE_START2 |
| F379 / F37B | $FEF378.w / $FEF37A.w | b,b | E | usados por el comando B (índice en C10E) |
| F4FF | $FEF4FE.w | b | E | ≠0 → FINISH no suma bonificación (68k `$3114` lo pone a 1: "tiempo agotado/etapa fallida") |
| F500:F501 | $FEF500.w | w | E/S | firma: 68k escribe $0001, MCU responde $A53E |

---------------------------------------------------------------------------------------------------------------------

## 3. GUION (0x3824): intérprete de disparadores por posición

Se ejecuta **una vez por petición** y procesa **como mucho una entrada** (con sus comandos encadenados).

```
GUION:
  if [C1C7] != 0 return                                  ; guion parado / etapa terminada
  base = BASES[ [C16F] ]                                 ; word BE en 4044 + 2*etapa
  off  = [C16C]<<8 | [C16D]
  p    = base + off                                      ; puntero en ROM del MCU
  b0 = rom[p] ; b1 = rom[p+1] ; cmd = rom[p+2]
  eje = b0 & $C0
  switch eje:
    $00: pos = ([EA98]<<8|[EA99]) + $30 ; pos &= $0FFF ; thr = (b0&$3F)<<8 | b1
    $80: pos = ([EA9C]<<8|[EA9D]) + $18 ; pos &= $3FFF ; thr = (b0&$3F)<<8 | b1
    $40: pos = ([EAA0]<<8|[EAA1]) + $18 (16 bits)       ; thr = (b0&$3F)<<8 | b1
    $C0: pos = ([EAA4]<<8|[EAA5])        (16 bits)       ; thr = (b0&$3F)<<8 | b1 ; if b0&$20: thr |= $C000 (signo)
  menor = (pos <s thr)                                   ; comparación CON SIGNO de 16 bits (rutina $3F7F)
  if cmd & $80 : if menor return      ; bit7=1: se dispara cuando pos >= thr
  else         : if !menor return     ; bit7=0: se dispara cuando pos <  thr
  ; ---- ejecutar la cadena de comandos ----
  q = 2
  loop:
    c = rom[p+q] ; k = c & $1F
    if (c & $60) == $60 : [C16C]=[C16D]=0 ; return            ; END (no aparece en los guiones)
    if (c & $60) == $40 : off += 4 ; store(off) ; return      ; SKIP (no aparece)
    h = ejecutar comando k con parámetros rom[p+q+1 ..]       ; h = bytes extra que consume (ver tabla)
    if k == 8 : return                                        ; CHECKPOINT ya ha guardado el offset
    q += h
    if c & $20 : q += 2 ; goto loop                           ; encadenar: siguiente comando en p+q
    off += q + 2 ; store(off) ; return                        ; último: avanza la entrada entera
store(off): [C16C] = off>>8 ; [C16D] = off & $FF
```
Tamaño de cada comando = 2 + h bytes (byte de comando + 1 parámetro mínimo). Entrada = 2 bytes de disparo + suma de
comandos. Los guiones no tienen marca de fin: la última entrada usa un disparo imposible (`EAA4 < -$368` tras un
FINISH que ya puso `C1C7=2`).

### 3.1 Comandos (k = cmd & $1F; p0 = rom[p+q+1], p1 = +2, …)

| k | h | Nombre | Efecto exacto |
|---|---|---|---|
| 0, $D-$1F | 2 | **SIGN** (señal copiloto) | `C273=p0 ; C27B=FLAG_SEÑAL[p0] ; C271=3 ; C275=(p2 rol 3)=p2·8 ; C277=p1·8` |
| 1 | 0 | C271=1 | `C271=1` (prepara/borra señal) |
| 2 | 0 | **SETDIR** | `C22F=p0&$3F ; EB1B=C22F ; C217=0 ; C229=2 ; EB4F=0 ; EB51=0 ; C231=0` |
| 3 | 4 | **GOTO_PT** | `C21D=p0&3 ; C22D=0 ; C229=1 ; C215=(p0&$80)?1:0 ; C218=p1 ; C219=p2 ; C21A=p3 ; C21B=p4 ; EB51++` |
| 4 | 2 | **RACE_START** | ver 3.2 |
| 5 | 2 | **RACE_START2** | ver 3.2 |
| 6 | 0 | NOP | — |
| 7 | 0 | **FINISH** | ver 3.3 |
| 8 | (6) | **CHECKPOINT** | `EB1C:EB1D = (p1<<8|p2) − $18 ; EB1E:EB1F = (p3<<8|p4) ; off += (q−2)+8 ; C16C=EB20=off ; return` |
| 9 | 0 | C105=1 | `C105=1` (inerte en este 68k) |
| $A | 0 | **PROGRESO** | `C14B=p0 ; C149=1` (p0 = 1..24, uno por tramo; el 68k en `$3D06` mueve el marcador de la barra Start→Finish) |
| $B | 0 | **LUZ/COLOR** | `C1A1=p0 ; v=$0455 ; if p0≠0 { v=$0277 ; a=[F379] ; if a==2 v=$0165 } else a=0 ; i=((3·a)+[F37B])·4 (mód 256) ; C10E[i..i+3] = v,v` |
| $C | 0 | NOP | — |

Frecuencia en los 12 guiones: GOTO_PT 422, SETDIR 239 (p0 ∈ {$0F,$14,$19,$1E,$23}), SIGN 441 (p0 ∈ 0..$50 y $8F,$91),
PROGRESO 24 por etapa (1..$18), CHECKPOINT 441, RACE_START/2 27 pares, LUZ 7 (p0 0/1), C271=1 230, NOPC 113,
FINISH 11 (la etapa 11 no tiene FINISH: su guion acaba en un SIGN con disparo normal). GOTO_PT p0 ∈ {00,01,02,80,81,82}
⇒ C21D ∈ {0,1,2} (la entrada 3 de ZONA, solapada, nunca se usa).

### 3.2 RACE_START (k=4, 0x3C50) y RACE_START2 (k=5, 0x3DAC)
```
RACE_START(p0, p2):                         ; p1 no se usa
  if [EB0B] == 1 { [EB0D] = 1 ; goto fin }
  [EAED]=p0 ; [EAE9]=1
  d = [C22F] ; [EAB1]=d ; [EA97]=d ; [EADB]=d ; [EABC:EABD] = d*30
  [EBD7]=0 ; [EB09]=0 ; [C22D]=1 ; [EAD1]=0 ; [EAC5]=0 ; [EAC7]=0 ; [EAF2..EAF5]=0
  i = (([EBDD] + 4) / 10) * 4                 ; división entera de 8 bits
  k = MULT[405E + i] (word BE)                ; ej. EBDD=100 → i=40 → k=$00ED
  m = (k * p2) >> 8                           ; 16 bits: hi(k_lo*p2) + k_hi*p2
  [EAF6..EAF9] = m * [EAD2:EAD3]              ; producto 16x16 → 32 bits sin signo
fin:
  (avanza 2) ; VELOCIDAD()                    ; escribe EAA8/EAAC desde EAB1 y EAD2

RACE_START2(p0, p2):
  [C22D]=0
  if [EB0D] == 1 { [EB0D]=0 ; goto fin2 }
  [EAF1]=p0 ; [EAE9]=0 ; [EBE7]=$87 ; [EBE5]=1 ; [EAFF]=0 ; [EAEB]=1
  [EB02..EB05] = [EAF2..EAF5]
  [EB00:EB01] = [EAF6..EAF9] / 12000           ; DIV32x16, cociente 16 bits
  i = ((([EBDD]+4)/10)*4) + 2 ; k2 = MULT[405E+i] ; m2 = (k2*p2)>>8
  [EAFA..EAFD] = m2 * [EAD2:EAD3]
fin2: (avanza 2)
```
Comprobado en traza: EBDD=100, p2=$B4, EAD2=$654 → EAF6=$00041A78; p2=$8C → EAFA=$00033CFC.

### 3.3 FINISH (k=7, 0x3A9C)
```
[C1B3]=0
if [F4FF] == 0:
   t = $0E0A - [C1AA:C1AB]        ; 3594 − tiempo transcurrido
   if no hubo acarreo (t >= 0):   bonus = t*3 ; [C1CA..C1CD] += bonus   (32 bits BE)
[C1C7] = 2                         ; el guion deja de ejecutarse hasta que el 68k lo reactive
[C268..C26B] = [C244..C247] ; [C26C..C26F] = [C252..C255]
```

---------------------------------------------------------------------------------------------------------------------

## 4. AUTOPILOTO (0x018E) — se ejecuta en cada petición tras GUION

```
AUTOPILOTO:
  [C22B] = 0
  switch [C229]:
    0: return
    2: goto ESTADO2
    1: goto ESTADO1
```

### 4.1 ESTADO1 — "ir hacia el punto (C218,C21A)" (0x01A0-0x0527)
```
  dx = (([EAA0]<<8|[EAA1]) + $18) - ([C218]<<8|[C219])      ; int16, guardado en $46:$47
  dy = ([EAA4]<<8|[EAA5]) - ([C21A]<<8|[C21B])               ; int16, guardado en $4A:$4B
  d2 = |dx|*|dx| + |dy|*|dy|                                 ; 32 bits (0x1EE..0x2DA)
  z = 0 ; T = ZONA[ [C21D] ] (3 longs en 07EC + 12*C21D)
  for i in 0..2: if d2 < T[i] break ; z++                    ; z = zona 0..3
  if ((dx>>8) + (dx&$FF)) & $FF == 0 return                  ; "dx==0" (test aproximado; no escribe C20D)
  ; --- cuadrante y tabla atan ---
  if dx < 0: dx = -dx ; if dy < 0 { dy=-dy ; tab=0868 } else tab=0894
  else     : if dy < 0 { dy=-dy ; tab=083C } else tab=0810
  q = (dy << 8) / dx                                         ; DIV32x16, cociente 16 bits (dividendo 00:dyH:dyL:00)
  e = primera entrada de tab (11 entradas de 4 bytes: ratio.w, a.b, b.b) con q < ratio ; si ninguna, la última
  obj = [C215] ? e.b : e.a                                    ; rumbo 0..39
  cur = [EAB1]
  if [C215] == 0:
     obj += {z0:-2, z1:-1, z2:+1, z3:+2} (mód 40)
     pasos = (obj - cur) mód 40
     bit = [C1FC] & 2
  else:
     obj += {z0:+2, z1:+1, z2:-1, z3:-2} (mód 40)
     pasos = (cur - obj) mód 40
     bit = [C1FC] & 1
  if 0 < pasos < 20:
     [C20D] = [C20F]
     if bit != 0 { [C20D] = 2 ; [C22B] = 2 }
  else:
     [C20D] = 0 ; [C22B] = 1
     if bit != 0 { [C20D] = 3 ; [C22B] = 2 }
  return
```
(Los signos de dx/dy se toman del bit 15 antes de negar; la negación es 16 bits.)

### 4.2 ESTADO2 — "orientar el coche al rumbo C22F" (0x053A-0x0617)
```
  if [EAD1] == 0 { [C229]=0 ; [C22D]=0 ; [C233]=[C22F] ; return }     ; fin del estado
  obj = [C22F] ; cur = [EAB1]
  if [C215] != 1: pasos = (obj-cur) mód 40 ; bit = [C1FC] & 1
  else          : pasos = (cur-obj) mód 40 ; bit = [C1FC] & 2
  if 0 < pasos < 20:
     [C20D] = $0A
     if bit == 0 [C20D] = 1
     return
  ; alineado (o a más de 180°)
  if bit == 0 { [C20D] = 3 ; return }
  if [C231] == 1 { [C22D] = 1 ; return }                                ; tras 20 frames bloquea la dirección
  [C20D] = 0 ; [C22B] = 1
  c = [C231] ; if c == 0 c = $15 ; [C231] = c - 1                        ; cuenta 20,19,…,1
  [EAB1] = [C22F] ; [EABC:EABD] = [C22F] * 30
  VELOCIDAD()
  return
```

### 4.3 VELOCIDAD (0x0618) — vector velocidad a partir de rumbo y velocidad escalar
```
  d = [EAB1] ; s = SENO[08C0 + 4d] (int16 BE, $7FFF=1.0) ; c = SENO[08C0 + 4d + 2]
  v = [EAD2:EAD3] (uint16)
  P = |s| * v  (32 bits) ; if s<0 P = -P
  [EAA8..EAAB] = EXT(P)  ; [EAAC..EAAF] = EXT(|c|*v con signo de c)
  donde EXT(P): bytes P3:P2:P1:P0 → resultado = (P3<<1|c2):(P2<<1|c1):(P1<<1|cin) con byte alto $FF si sale acarreo de P3, $00 si no
        ; es decir ≈ (P >> 8) << 1 con extensión de signo; cin = acarreo residual (normalmente 1 → byte bajo impar)
```
Equivale a la rutina del 68k en `$6A0E` (`EAA8 = tabla[EAB0*4]*EAD2 >> 7`, `EAAC = tabla[EAB0*4+2]*EAD2 >> 6|7`),
que usa su propia tabla en ROM `$E0022` con el mismo índice. Para el port basta llamar a la rutina del 68k (o
replicar bit a bit la de arriba si se quiere exactitud absoluta; la diferencia es el bit 0 y el desplazamiento de EAAC).

### 4.4 DIV32x16 (0x079C)
Resta-desplaza 16 iteraciones: dividendo $34:$35:$36:$37, divisor $32:$33 → cociente $36:$37 (16 bits, se
trunca), resto $34:$35.

---------------------------------------------------------------------------------------------------------------------

## 5. Tablas de datos (offsets en `wrdallas.bin`)

| Offset | Tamaño | Tabla | Contenido |
|---|---|---|---|
| 07EC | 3×12 (+1 solapada) | ZONA[C21D][3] (long BE) | 0: 00001900 00002400 00003100 (80², 96², 112²) · 1: 00006E40 00008440 00009C40 (168², 184², 200²) · 2: 000120A6 00014400 00016900 (≈272², 288², 304²) · 3 (no usada): 00141400 003D1327 006A1226 |
| 0810 | 11×4 | ATAN dx≥0,dy≥0: (ratio.w, a, b) | 0014:14,00 · 003D:13,27 · 006A:12,26 · 009D:11,25 · 00DB:10,24 · 012C:0F,23 · 01A2:0E,22 · 026A:0D,21 · 042A:0C,20 · 0CB5:0B,1F · 7FFF:0A,1E |
| 083C | 11×4 | ATAN dx≥0,dy<0 | mismos ratios; (a,b) = 14,00 · 15,01 · 16,02 · 17,03 · 18,04 · 19,05 · 1A,06 · 1B,07 · 1C,08 · 1D,09 · 1E,0A |
| 0868 | 11×4 | ATAN dx<0,dy<0 | (a,b) = 00,14 · 27,13 · 26,12 · 25,11 · 24,10 · 23,0F · 22,0E · 21,0D · 20,0C · 1F,0B · 1E,0A |
| 0894 | 11×4 | ATAN dx<0,dy≥0 | (a,b) = 00,14 · 01,15 · 02,16 · 03,17 · 04,18 · 05,19 · 06,1A · 07,1B · 08,1C · 09,1D · 0A,1E |
| 08C0 | 40×4 | SENO/COSENO: (sin, cos) int16 BE, $7FFF = 1.0, paso 9° | 0000 7FFF · 1405 7E6B · 278D 79BB · 3A1B 720B · 4B3B 678D · 5A81 5A81 · 678D 4B3B · 720B 3A1B · 79BB 278D · 7E6B 1405 · 7FFE 0000 · … (simétrica; dir 20 = 0000 8001, dir 30 = 8001 0000) |
| 3FA4 | 92 bytes | FLAG_SEÑAL[tipo] | 1 para tipo ∈ {0F,10,1F,20,21,22,3D,3E,3F,40,41,42,43,44}; 0 el resto |
| 4044 | 12×2 | BASES (word BE) | 0960 0D90 11A6 157C 193E 1D82 2160 2524 28B4 2D10 30D6 3466 |
| 405C | 2 | constante | 2EE0 (12000) |
| 405E | 11×4 | MULT[(EBDD+4)/10] = (a.w, b.w) | 0103,0102 · 0100,0100 · 00FC,00FC · 00FB,00FC · 00F9,00FA · 00F6,00F8 · 00F4,00F6 · 00F2,00F4 · 00F0,00F2 · 00EF,00F2 · 00ED,00F0 |
| 0960-37FF | 12 guiones | ver `scripts.txt` | longitudes (bytes hasta la entrada terminal): 0x43A 0x420 0x3E0 0x3CC 0x44E 0x3E8 0x3CE 0x39A 0x466 0x3D0 0x39A 0x39C |

Los ratios de ATAN son tan(4.5°·(2k+1))·256: 20, 61, 106, 157, 219, 300, 418, 618, 1066, 3253, ∞ → sectores de 9°.
Convención de rumbos: 40 direcciones, dir 0 = (sin 0, cos 1), dir 10 = (1,0), dir 20 = (0,−1), dir 30 = (−1,0);
`EAA8 ← sin`, `EAAC ← cos`, coherente con la rutina del 68k en `$6A0E`.

---------------------------------------------------------------------------------------------------------------------

## 6. Timing (medido con `t2/timing.lua`, 473 peticiones de la demo)

* 68k escribe `C103=1` → el MCU lo ve en 1-27 µs (media 13 µs; sondeo a ~34 kHz).
* Proceso (C103 visto → `C101=1`): mín 138 µs, media 611 µs, máx 1,80 ms (el máximo corresponde a frames con
  GOTO_PT/estado 1 con división + comandos encadenados).
* Total petición→respuesta: 0,14-1,81 ms, siempre dentro del mismo frame (16,7 ms).
* El 68k emite la petición en `$3634` (final de su lógica de frame) y comprueba `$FEC100` en `$33F0` tras el VBlank
  del frame siguiente: en 7200 frames de traza nunca encontró 0 (1 lectura por frame). **Los resultados del MCU para
  la petición del frame N los usa el 68k en el frame N+1.** En el port, ejecutar la lógica del MCU síncronamente en
  `$3634` (y dejar C101=1) reproduce exactamente el comportamiento observable, porque entre `$3634` y `$33F0` el 68k
  sólo copia `C244→C248`/`C252→C258` y no toca ninguna variable de salida del MCU.

---------------------------------------------------------------------------------------------------------------------

## 7. Verificación dinámica en MAME (resumen)

Taps sobre `":gaelco_ds5002fp".spaces["hostmem"]` rango 0-0xFFFF (ojo: el MCU direcciona 0xC000-0xFFFF; un tap
0-0x3FFF sólo ve el barrido ocioso) y taps del 68k sobre `$FEC100-$FEC107`, `$FEC16C-$FEC16F`, `$FEC1AA`, `$FEC1C6`,
`$FEC1FC`, `$FEC20C-$FEC233`, `$FEC266`, `$FEEA96-$FEEAB1`, `$FEEAD0-$FEEAD5`, `$FEEB0A-$FEEB0D`, `$FEEBDC`,
`$FEF378-$FEF37B`, `$FEF4FE-$FEF501`. (`scr.vpos`/`scr:vpos()` no existen en 0.264 Lua; `cpu.state["CURPC"]` sí.)

Partida real (tap4, 120 s, moneda+start a los 10 s, `-nocoin_lockout` necesario porque el juego mantiene el lockout
activo; acelerador fijo, volante alternado): 5105 peticiones (1/frame desde el inicio de la carrera, 0 fuera de ella),
C16F=5 (etapa jugada), 96 avances de puntero de guion (todos reproducidos por `decode_script.py`).

Por petición el MCU lee siempre: `C103, C1C7, C16F, C16C, C16D`, los 2 bytes de la coordenada del disparo, `C229`;
en estado 1 además `EAA0/1, C218/9, EAA4/5, C21A/B, C21D, C215×2, EAB1, C20F, C1FC` (16 lecturas) y escribe
`C22B, C20D (1-2 veces), C22B`; en estado 2 `EAD1, C22F, EAB1, C215, C1FC, C231` y escribe `C20D`/`C22D`/`C231`/
`EAB1`/`EABC`/`EAA8-EAAF`. Siempre escribe `C103=0, C101=0, C22B=0, C101=1`.

Valores observados (partida): C229 ∈ {0:3472, 1:1490, 2:143} frames; C20D ∈ {0:333, 2:1108, 3:139, $A:3, $B:1215}
(C20F=$B); C22B ∈ {1:333, 2:1247}; C22D=1 70 veces; C231 cuenta 20→0; EAB1 0E..23; EAD2 ≈ $0400-$0654;
EBDD=$14 (partida) / $64 (demo); F500/F501 respondidos 501 veces en los 10 s de self-test (el 68k reescribe 1 en
cada pasada); C105 escrito 1 vez; comandos LUZ 0 veces en la etapa 5 (aparecen en otras etapas).
Demo de atracción (tap3): idéntico, con `C1FC` procedente de la tabla de entradas grabadas (`$3430`) y el guion
arrancando en el offset `C17A` = $00D0.

---------------------------------------------------------------------------------------------------------------------

## 8. Lo que hay que reimplementar en 68k (checklist)

1. Self-test: al escribir `$0001` en `$FEF500` dejar `$A53E` (o inicializar `$FEF500=$A53E` y hacer que la escritura
   de 1 lo vuelva a poner). `$FEEAD4 = 0` siempre (nunca "Tension baja"). `$FEC000` no importa.
2. En `$8DF2` ya se pone `C101=1`; en `$3634`, en vez de `move.w #1,$FEC102`, llamar a `MCU_FRAME`:
   `C103=0 ; C101=0 ; GUION ; AUTOPILOTO ; C101=1` (sección 3 y 4). Mantener `tst.w $FEC100` en `$33F0` (pasa).
3. Copiar de `wrdallas.bin` a la ROM del port: 0x07EC-0x095F (tablas), 0x0960-0x37FF (guiones), 0x3FA4-0x3FFF,
   0x4044-0x4089. Direccionar los guiones con `BASES[C16F] + C16C` (punteros de 16 bits relativos al binario; en 68k
   sumar la base de la copia).
4. Aritmética a replicar bit a bit: comparación con signo del disparo (y el sign-extend sólo en el eje `$C0`),
   máscaras `$0FFF`/`$3FFF` y sumas de `$30`/`$18` sobre `EA98`/`EA9C`/`EAA0`; división `(dy<<8)/dx` truncada a 16
   bits; `(k·p2)>>8`; `EAF6/12000` truncado a 16 bits; el test `((dx>>8)+(dx&$FF))&$FF==0`; mód 40 por
   incrementos; `C231` 21→1; `EBDD+4)/10` en 8 bits.
5. `VELOCIDAD()` puede reemplazarse por la rutina del 68k `$6A0E` (misma tabla/índice) salvo que se quiera el
   detalle del bit 0 y el `>>6` de `EAAC`.
6. Nada más: el MCU no toca sprites, VRAM, sonido, entradas, rivales, cronómetro ni aleatorios.
