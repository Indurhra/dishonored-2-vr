import argparse,json
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
p=argparse.ArgumentParser();p.add_argument('target');p.add_argument('warped');p.add_argument('output');a=p.parse_args()
m={s.split()[0]:s.split()[1:] for s in Path(a.target+'_metadata.txt').read_text().splitlines()};w,h=int(m['width'][0]),int(m['height'][0])
v=np.fromfile(a.target+'_color.bin','<u4').reshape(h,w)
def f(v,b):
 e=(v>>b)&31;t=v&((1<<b)-1);return np.where(e==0,np.ldexp(t.astype(float),1-15-b),np.ldexp((t+(1<<b)).astype(float),e.astype(int)-15-b))
reference=np.stack([f(v&2047,6),f((v>>11)&2047,6),f(v>>22,5)],-1)
warp=np.fromfile(a.warped,'<f2').reshape(h,w,4).astype(float);valid=warp[:,:,3]>0
rawDepth=np.fromfile(a.target+'_depth.bin','<u4').reshape(h,w);fg=(rawDepth>>24)>0;sky=(rawDepth&0xffffff)==0
report={label:{'pixels':int(mask.sum()),'uncovered_fraction':float((~valid)[mask].mean())} for label,mask in [('all',np.ones_like(valid)),('foreground',fg),('sky',sky),('world',~fg&~sky)]}
report['covered_log_color_error_median']=float(np.median(np.abs(np.log1p(reference)-np.log1p(warp[:,:,:3]))[valid]))
def display(rgb):return np.power(np.clip(rgb/(1+rgb),0,1),1/2.2)
shown=display(warp[:,:,:3]);shown[~valid]=[1,0,1]
diff=np.clip(np.abs(display(reference)-display(warp[:,:,:3]))*4,0,1);diff[~valid]=[1,0,1]
canvas=Image.new('RGB',(1200,460));d=ImageDraw.Draw(canvas)
for i,(name,img) in enumerate([('Native target',display(reference)),('Fresh-eye GPU warp (holes magenta)',shown),('Absolute display difference x4',diff)]):
 im=Image.fromarray(np.uint8(img*255));im.thumbnail((400,430));canvas.paste(im,(i*400,25));d.text((i*400+4,5),name,fill='white')
canvas.save(a.output);Path(a.output+'.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
