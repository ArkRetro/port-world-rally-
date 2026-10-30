import sys,glob
from PIL import Image
fs=sorted(glob.glob(sys.argv[1]+'/*.png'))[int(sys.argv[3]):int(sys.argv[4])]
c=6; w,h=160,112; r=(len(fs)+c-1)//c
o=Image.new('RGB',(w*c,h*r))
from PIL import ImageDraw
d=ImageDraw.Draw(o)
for i,f in enumerate(fs):
  im=Image.open(f).resize((w,h)); o.paste(im,((i%c)*w,(i//c)*h)); d.text(((i%c)*w+2,(i//c)*h+2),f[-10:-4],fill=(255,0,0))
o.save(sys.argv[2])
