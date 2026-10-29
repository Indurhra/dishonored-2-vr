"""Compare same-eye captured motion vectors against depth-derived camera motion."""
import argparse,json
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('previous');p.add_argument('current');a=p.parse_args()
def load(base):
 m={s.split()[0]:s.split()[1:] for s in Path(base+'_metadata.txt').read_text().splitlines()}
 w,h=int(m['width'][0]),int(m['height'][0]);raw=np.fromfile(base+'_depth.bin','<u4').reshape(h,w)
 v=np.array(m['view'],float).reshape(4,4).T;pr=np.array(m['projection'],float).reshape(4,4)
 return m,w,h,raw,pr@v
pm,pw,ph,praw,pvp=load(a.previous);cm,w,h,raw,vp=load(a.current)
if pm['eye']!=cm['eye'] or int(cm['serial'][0])-int(pm['serial'][0])!=2 or (pw,ph)!=(w,h):raise SystemExit('Need consecutive same-eye history at identical dimensions')
y,x=np.mgrid[0:h:4,0:w:4];d=(raw[y,x]&0xffffff)/16777215.
q=np.stack([2*(x+.5)/w-1,1-2*(y+.5)/h,d,np.ones_like(d)],-1)@(pvp@np.linalg.inv(vp)).T
ndc=q[...,:3]/q[...,3,None];px=(ndc[...,0]+1)*w/2-.5;py=(1-ndc[...,1])*h/2-.5
ix=np.rint(px).astype(int).clip(0,w-1);iy=np.rint(py).astype(int).clip(0,h-1);pd=(praw[iy,ix]&0xffffff)/16777215.
valid=(raw[y,x]>>24==0)&(praw[iy,ix]>>24==0)&(d>0)&(q[...,3]>0)&(px>=1)&(px<w-2)&(py>=1)&(py<h-2)&(np.abs(ndc[...,2]-pd)<np.maximum(pd*.005,2/16777215))
expected=np.stack([px-x,py-y],-1)[valid]
mv=np.fromfile(a.current+'_motion.bin','<f2').reshape(h,w,2)[y,x].astype(float)[valid]*[w,h]
# Engine projection adds 2*jitter at [0,2]/[1,2] with clip W=-Z.
# In top-left pixel coordinates the actual raster offset is (-jx*w,+jy*h).
pj=np.array(pm['native_jitter'],float)*[-w,h];cj=np.array(cm['native_jitter'],float)*[-w,h]
unjittered=expected-(pj-cj)
result={'samples':int(valid.sum()),'expected_motion_pixels_median':np.median(np.linalg.norm(expected,axis=1)).item(),'sign_candidates':{}}
for sx,sy in [(1,1),(-1,-1),(1,-1),(-1,1)]:
 error=mv*[sx,sy]-expected
 result['sign_candidates'][f'{sx},{sy}']={'median_error_pixels':np.median(np.linalg.norm(error,axis=1)).item(),'median_xy_residual':np.median(error,axis=0).tolist(),'p95_error_pixels':np.quantile(np.linalg.norm(error,axis=1),.95).item()}
error=-mv-unjittered
result['negative_xy_without_raster_jitter']={'median_error_pixels':np.median(np.linalg.norm(error,axis=1)).item(),'median_xy_residual':np.median(error,axis=0).tolist(),'p95_error_pixels':np.quantile(np.linalg.norm(error,axis=1),.95).item()}
print(json.dumps(result,indent=2))
