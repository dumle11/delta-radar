#include "../skeleton-math.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void near(float a, float b) { assert(fabsf(a-b)<0.0001f); }
int main(void) {
    SkeletonTransform root = {.rotation={0,0,0.70710678118f,0.70710678118f},
        .translation={1000,2000,3000},.scale={2,3,1}};
    assert(skeleton_transform_valid(&root));
    float relative[3]={10,20,-86},origin[3];skeleton_transform_point(&root,relative,origin);
    near(origin[0],940);near(origin[1],2020);near(origin[2],2914);
    SkeletonTransform mesh={.rotation={0,0,1,0},.scale={1,2,3}};
    memcpy(mesh.translation,origin,sizeof(origin));
    float joint[3]={10,20,30},world[3];skeleton_transform_point(&mesh,joint,world);
    near(world[0],930);near(world[1],1980);near(world[2],3004);
    /* Packed FVector scale lives at byte 28. Poisoning the padding cannot
     * change rotation/scale validity or world-space coordinates. */
    memset(mesh.padding,0xff,sizeof(mesh.padding));assert(skeleton_transform_valid(&mesh));
    skeleton_transform_point(&mesh,joint,world);near(world[2],3004);
    mesh.rotation[3]=2;assert(!skeleton_transform_valid(&mesh));
    mesh.rotation[3]=0;mesh.translation[0]=NAN;assert(!skeleton_transform_valid(&mesh));
    puts("skeleton transform rotation, scale, attachment and invalid-data checks passed");return 0;
}
