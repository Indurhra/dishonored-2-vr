"""Check cross-eye depth correspondence using captured engine matrices; no fitted offsets."""
import argparse,json
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('source');p.add_argument('target');p.add_argument('--transform-out',type=Path);a=p.parse_args()
def read(base):
 m={s.split()[0]:s.split()[1:] for s in Path(base+'_metadata.txt').read_text().splitlines()}
 w,h=int(m['width'][0]),int(m['height'][0]);raw=np.fromfile(base+'_depth.bin','<u4').reshape(h,w)
 # Constant-buffer view is transposed relative to the engine row-major projection.
 v=np.array(m['view'],float).reshape(4,4).T;p=np.array(m['projection'],float).reshape(4,4)
 return w,h,raw&0xffffff,raw>>24,p@v
w,h,ds,ss,vs=read(a.source);tw,th,dt,st,vt=read(a.target)
if a.transform_out: np.savetxt(a.transform_out,(vt@np.linalg.inv(vs)).reshape(1,16),fmt="%.10g")
y,x=np.mgrid[0:h:4,0:w:4];z=ds[y,x]/16777215
clip=np.stack([2*(x+.5)/w-1,1-2*(y+.5)/h,z,np.ones_like(z)],-1)
projected=clip@(vt@np.linalg.inv(vs)).T
ndc=projected[:,:,:3]/projected[:,:,3,None]
tx=np.rint((ndc[:,:,0]+1)*tw/2-.5).astype(int);ty=np.rint((1-ndc[:,:,1])*th/2-.5).astype(int)
inside=(tx>=0)&(tx<tw)&(ty>=0)&(ty<th)&(projected[:,:,3]>0)&(z>0)
tx=tx.clip(0,tw-1);ty=ty.clip(0,th-1);actual=dt[ty,tx]/16777215
residual=np.abs(ndc[:,:,2]-actual)/np.maximum(actual,1e-10)
result={}
for label,mask in [('world',(ss[y,x]==0)&(st[ty,tx]==0)),('foreground',(ss[y,x]>0)&(st[ty,tx]>0))]:
 ok=inside&mask&(actual>0);r=residual[ok]
 result[label]={'samples':int(ok.sum()),'relative_depth_error_q50_q90_q99':np.quantile(r,[.5,.9,.99]).tolist(),'fraction_below_1percent':float((r<.01).mean())}
print(json.dumps(result,indent=2))
