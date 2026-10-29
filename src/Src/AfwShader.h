#pragma once
// D2-specific implementation of the fresh/held depth-seed and refinement approach
// studied in Dishonored 1 staging cdd8d32c5, core/gfx/afw_warp.cpp.
// Uses full clip transforms, reversed device depth and explicit stencil masks.
// No UE3 depth scale, camera basis or foreground distance threshold is imported.
inline constexpr char kAfwShader[]=R"HLSL(
Texture2D<float4> freshColor:register(t0);
Texture2D<float> freshDepth:register(t1);
Texture2D<uint> freshMask:register(t2);
Texture2D<float4> heldColor:register(t3);
Texture2D<float> heldDepth:register(t4);
Texture2D<uint> heldMask:register(t5);
Texture2D<uint> freshSeed:register(t6);
Texture2D<uint> heldSeed:register(t7);
RWTexture2D<uint> seedOut:register(u0);
RWTexture2D<float4> outputImage:register(u1);
SamplerState linearClamp:register(s0);
SamplerState pointClamp:register(s1);
cbuffer Parameters:register(b0){
 row_major float4x4 freshToTarget,targetToFresh,heldToTarget,targetToHeld;
 float4 sizes; // render width/height, target width/height
 float4 limits; // source selector, pixel tolerance, relative depth tolerance, depth quantization
 uint heldEnabled;uint foregroundAligned;uint2 padding;
};
float2 ndc(float2 uv){return uv*float2(2,-2)+float2(-1,1);}
float2 uvOf(float2 xy){return xy*float2(.5,-.5)+.5;}
[numthreads(8,8,1)]void Seed(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=uint2(sizes.xy)))return;
 float2 uv=(id.xy+.5)/sizes.xy;bool held=limits.x>0.5;
 float depth=held?heldDepth.Load(int3(id.xy,0)):freshDepth.Load(int3(id.xy,0));
 float4 target=mul(held?heldToTarget:freshToTarget,float4(ndc(uv),depth,1));
 if(target.w<=0 || !all(isfinite(target)))return;
 float3 q=target.xyz/target.w;if(depth==0)q.z=0;else if(q.z<0 || q.z>1)return;
 int2 pixel=int2(floor(uvOf(q.xy)*sizes.zw-.5));
 uint encoded=asuint(q.z)+1; // zero is an empty seed; encoded one represents sky
 [unroll]for(int y=0;y<2;y++)[unroll]for(int x=0;x<2;x++){
   int2 at=pixel+int2(x,y);if(all(at>=0)&&all(at<int2(sizes.zw)))InterlockedMax(seedOut[at],encoded);
 }
}
struct Candidate{float2 uv;float depth;bool valid;bool foreground;};
Candidate Resolve(float2 targetUV,bool held,uint seed){
 Candidate c=(Candidate)0;if(seed==0)return c;
 float4 a=mul(held?targetToHeld:targetToFresh,float4(ndc(targetUV),0,1));
 float4 b=mul(held?targetToHeld:targetToFresh,float4(0,0,1,0));
 float z=asfloat(seed-1);float2 uv=0;
 [unroll]for(int i=0;i<5;i++){
   float4 q=a+z*b;if(q.w<=0)return c;uv=uvOf(q.xy/q.w);
   if(any(uv<0)||any(uv>1))return c;
   float d=held?heldDepth.SampleLevel(pointClamp,uv,0):freshDepth.SampleLevel(pointClamp,uv,0);
   float denominator=b.z-d*b.w;if(abs(denominator)<1e-12)return c;
   z=(d*a.w-a.z)/denominator;
 }
 float4 q=a+z*b;if(q.w<=0)return c;uv=uvOf(q.xy/q.w);
 if(any(uv<0)||any(uv>1))return c;
 float d=held?heldDepth.SampleLevel(pointClamp,uv,0):freshDepth.SampleLevel(pointClamp,uv,0);
 float4 check=mul(held?heldToTarget:freshToTarget,float4(ndc(uv),d,1));
 if(check.w<=0 || !all(isfinite(check)))return c;
 float2 error=(uvOf(check.xy/check.w)-targetUV)*sizes.zw;
 if(max(abs(error.x),abs(error.y))>limits.y)return c;
 c.uv=uv;c.depth=d==0?0:check.z/check.w;c.valid=c.depth>=0&&c.depth<=1;
 int2 maskPixel=clamp(int2(uv*sizes.xy),int2(0,0),int2(sizes.xy)-1);
 c.foreground=(held?heldMask.Load(int3(maskPixel,0)):freshMask.Load(int3(maskPixel,0)))!=0;
 return c;
}
[numthreads(8,8,1)]void Compose(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=uint2(sizes.zw)))return;
 float2 uv=(id.xy+.5)/sizes.zw;
 Candidate f=Resolve(uv,false,freshSeed.Load(int3(id.xy,0)));
 Candidate h=(Candidate)0;if(heldEnabled)h=Resolve(uv,true,heldSeed.Load(int3(id.xy,0)));
 if(h.valid){
   float4 q=mul(targetToFresh,float4(ndc(uv),h.depth,1));
   if(q.w>0){float2 at=uvOf(q.xy/q.w);if(all(at>=0)&&all(at<=1)){
     float freshZ=freshDepth.SampleLevel(pointClamp,at,0);
     // A held point in front of the newly visible surface is stale.
     if(q.z/q.w>freshZ+max(limits.w,abs(freshZ)*limits.z))h.valid=false;
   }}
 }
 // Moving foreground comes from the fresh eye. Never retain a stale held weapon.
 bool alignedHand=foregroundAligned && h.valid && h.foreground && (!f.valid || (f.foreground && abs(f.depth-h.depth)<=max(limits.w,abs(h.depth)*limits.z)));
 bool useFresh=f.valid&&!alignedHand&&(f.foreground||!h.valid||h.foreground||f.depth>h.depth+max(limits.w,abs(h.depth)*limits.z));
 if(useFresh)outputImage[id.xy]=float4(freshColor.SampleLevel(linearClamp,f.uv,0).rgb,1);
 else if(h.valid&&(!h.foreground||alignedHand))outputImage[id.xy]=float4(heldColor.SampleLevel(linearClamp,h.uv,0).rgb,1);
 else outputImage[id.xy]=float4(0,0,0,0); // explicit hole; caller must not hide failed reconstruction
}
)HLSL";
