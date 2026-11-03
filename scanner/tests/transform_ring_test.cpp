#include "../tools/transform_ring.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
static void fixture(float slab[27][12],int first,float x,float y,float z) {
 memset(slab,0,27*12*sizeof(float));
 // Real captured geometry is rounded at centimetre precision.
 for(int i=0;i<14;i++) {
  float *v=slab[first+i];v[3]=1;v[7]=v[8]=v[9]=1;
  double angle=6.283185307179586*i/14;
  v[4]=x+(float)std::round(4918*std::cos(angle));
  v[5]=y+(float)std::round(4918*std::sin(angle));v[6]=z;
 }
}
int main() {
 float slab[27][12],out[3];int found=-1;
 for(int first=0;first<=13;first++) {
  fixture(slab,first,-12345,45678,-932);
  assert(delta_transform_ring_center(slab,out,&found));assert(found==first);
  assert(std::fabs(out[0]+12345)<0.1&&std::fabs(out[1]-45678)<0.1&&out[2]==-932);
 }
 fixture(slab,4,100,200,300);slab[5][4]+=50;assert(!delta_transform_ring_center(slab,out));
 fixture(slab,4,100,200,300);slab[5][6]+=50;assert(!delta_transform_ring_center(slab,out));
 fixture(slab,4,100,200,300);slab[5][8]=0;assert(!delta_transform_ring_center(slab,out));
 fixture(slab,4,100,200,300);slab[5][10]=1;assert(!delta_transform_ring_center(slab,out));
 fixture(slab,4,100,200,300);memcpy(slab[5],slab[6],sizeof(slab[5]));assert(!delta_transform_ring_center(slab,out));
 fixture(slab,4,100,200,300);slab[5][4]=std::numeric_limits<float>::quiet_NaN();assert(!delta_transform_ring_center(slab,out));
 memset(slab,0,sizeof(slab));assert(!delta_transform_ring_center(slab,out));
 puts("14 candidate indices and 7 malformed/empty controls passed");
}
