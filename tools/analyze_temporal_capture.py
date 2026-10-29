"""Inspect a matched TemporalCapture. Output is local diagnostic evidence, not game assets."""
import argparse, json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
p=argparse.ArgumentParser();p.add_argument('base',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
meta={line.split()[0]:line.split()[1:] for line in Path(str(a.base)+'_metadata.txt').read_text().splitlines()}
w,h=int(meta['width'][0]),int(meta['height'][0]);n=w*h
raw={k:np.fromfile(str(a.base)+'_'+k+'.bin',dtype='<u4') for k in ['color','motion','depth']}
assert all(v.size==n for v in raw.values()), 'Incomplete capture'
def unsigned_float(v,bits):
    mant=v&((1<<bits)-1);exp=(v>>bits)&31
    return np.where(exp==0,np.ldexp(mant.astype(np.float32),1-15-bits),np.ldexp((mant+(1<<bits)).astype(np.float32),exp.astype(np.int32)-15-bits))
c=raw['color'];rgb=np.stack([unsigned_float(c&2047,6),unsigned_float((c>>11)&2047,6),unsigned_float(c>>22,5)],-1).reshape(h,w,3)
d=(raw['depth']&0xffffff).astype(np.float32).reshape(h,w)/16777215
stencil=(raw['depth']>>24).reshape(h,w)
mv=raw['motion'].view('<f2').reshape(h,w,2).astype(np.float32)
pixels=mv*np.array([w,h]);mag=np.linalg.norm(pixels,axis=2)
report={'serial':int(meta['serial'][0]),'eye':int(meta['eye'][0]),'size':[w,h],
 'depth_quantiles':np.quantile(d,[0,.01,.5,.95,.99,1]).tolist(),
 'stencil_values_counts':dict(zip(*[x.tolist() for x in np.unique(stencil,return_counts=True)])),
 'motion_finite_fraction':float(np.isfinite(mv).all(2).mean()),
 'motion_pixels_quantiles':np.quantile(mag[np.isfinite(mag)],[0,.5,.95,.99,1]).tolist(),
 'color_quantiles':np.quantile(rgb,[0,.5,.95,.99,1]).tolist(),
 'depth_samples':{name:float(d[int(y*h),int(x*w)]) for name,x,y in [('sky',.5,.1),('building',.3,.4),('floor',.5,.85),('right_foreground',.86,.72),('left_foreground',.32,.70)]}}
a.output.mkdir(parents=True,exist_ok=True)
(a.output/'report.json').write_text(json.dumps(report,indent=2))
# Display transformations only; never feed these normalized images back into rendering.
color=np.power(np.maximum(rgb,0)/(1+np.maximum(rgb,0)),1/2.2)
logd=np.log10(np.maximum(d,1e-8));lo,hi=np.quantile(logd,[.01,.99]);depth=np.clip((logd-lo)/max(hi-lo,1e-9),0,1)
motion=np.stack([np.clip(.5+pixels[:,:,0]/8,0,1),np.clip(.5+pixels[:,:,1]/8,0,1),np.clip(mag/8,0,1)],-1)
canvas=Image.new('RGB',(1600,460));draw=ImageDraw.Draw(canvas)
for i,(label,im) in enumerate([('HDR colour (display tonemap)',color),('Raw depth (log display)',np.repeat(depth[:,:,None],3,2)),('Motion (8 pixel display scale)',motion),('Stencil nonzero',np.repeat((stencil>0)[:,:,None],3,2))]):
    view=Image.fromarray(np.uint8(np.nan_to_num(im)*255));view.thumbnail((400,430));canvas.paste(view,(400*i,25));draw.text((400*i+5,5),label,fill='white')
canvas.save(a.output/'inputs.png');print(json.dumps(report,indent=2))
