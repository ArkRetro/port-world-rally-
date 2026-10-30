import sys,glob
from PIL import Image
d,out=sys.argv[1],sys.argv[2]; cols=int(sys.argv[3]) if len(sys.argv)>3 else 5
p=sorted(glob.glob(d+'/*.png'))
w,h=Image.open(p[0]).size; w//=2; h//=2
W=Image.new('RGB',(w*cols,h*((len(p)+cols-1)//cols)))
for i,a in enumerate(p): W.paste(Image.open(a).convert('RGB').resize((w,h)),((i%cols)*w,(i//cols)*h))
W.save(out)
