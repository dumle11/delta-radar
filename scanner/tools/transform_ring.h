#pragma once
#include <cmath>
#include <cstddef>
// Diagnostic recovery for the observed 14-entry transform ring. This operates
// within one spatial sample; it neither averages frames nor adds a position bias.
// Reject any slab that does not have a unique complete, evenly spaced ring.
static bool delta_transform_ring_center(const float slab[27][12], float out[3], int *first=nullptr) {
 int matches=0;float found[3]={};int start=0;
 for(int s=0;s<=13;s++) {
  bool valid=true;double center[3]={};
  for(int i=0;i<14&&valid;i++) {
   const float *v=slab[s+i];double norm=0;
   for(int j=0;j<12;j++)if(!std::isfinite(v[j]))valid=false;
   for(int j=0;j<4;j++){norm+=v[j]*v[j];if(std::fabs(v[j]-slab[s][j])>0.001f)valid=false;}
   if(norm<0.99||norm>1.01)valid=false;
   for(int j=7;j<=9;j++)if(std::fabs(v[j]-1)>0.001f)valid=false;
   if(v[10]!=0||v[11]!=0)valid=false;
   for(int j=0;j<3;j++)center[j]+=v[j+4]/14.0;
  }
  if(!valid)continue;
  double r2=0;
  for(int i=0;i<14;i++){double x=slab[s+i][4]-center[0],y=slab[s+i][5]-center[1];r2+=x*x+y*y;}
  double radius=std::sqrt(r2/14);
  if(radius<100||radius>100000)continue;
  double angles[14];
  for(int i=0;i<14;i++) {
   double x=slab[s+i][4]-center[0],y=slab[s+i][5]-center[1];
   if(std::fabs(std::hypot(x,y)-radius)>3||std::fabs(slab[s+i][6]-center[2])>1)valid=false;
   angles[i]=std::atan2(y,x);
  }
  for(int i=0;i<14;i++)for(int j=i+1;j<14;j++)if(angles[j]<angles[i]){double t=angles[i];angles[i]=angles[j];angles[j]=t;}
  const double pi=3.14159265358979323846;
  for(int i=0;i<14;i++) {
   double next=i==13?angles[0]+2*pi:angles[i+1];
   if(std::fabs(next-angles[i]-2*pi/14)>0.003)valid=false;
  }
  if(!valid)continue;
  matches++;start=s;for(int j=0;j<3;j++)found[j]=(float)center[j];
 }
 if(matches!=1)return false;
 for(int j=0;j<3;j++)out[j]=found[j];
 if(first)*first=start;
 return true;
}
