#ifndef INTERNAL_RENDER_PROJECTION_H
#define INTERNAL_RENDER_PROJECTION_H
/* Layout/ABI captured from PlayerController.ProjectWorldLocationToScreen:
 * wrapper 0x1ba5ccc4 -> 0x115d4360, LocalPlayer slot 0x2c0. */
typedef struct {
    float origin[3],padding;
    float rotation[16],projection[16],reserved_matrix[16];
    int32_t view_rect[4],rect[4];
} RPData;
_Static_assert(offsetof(RPData,rotation)==0x10,"projection rotation layout");
_Static_assert(offsetof(RPData,projection)==0x50,"projection matrix layout");
_Static_assert(offsetof(RPData,rect)==0xe0,"constrained viewport layout");
static struct {RPData data;float scale_x,scale_y;int valid,stage;} rp_frame;
static int render_projection_point(float,float,float,float *,float *);
static int rp_valid_data(const RPData *data) {
    if(data->view_rect[2]<1 || data->view_rect[3]<1 || data->view_rect[2]>16384 || data->view_rect[3]>16384 ||
        data->rect[0]<0 || data->rect[1]<0 || data->rect[2]<=data->rect[0] || data->rect[3]<=data->rect[1] ||
        data->rect[2]>data->view_rect[2] || data->rect[3]>data->view_rect[3]) return 0;
    for(unsigned i=0;i<3;i++) if(!isfinite(data->origin[i]) || fabsf(data->origin[i])>10000000) return 0;
    for(unsigned i=0;i<16;i++) if(!isfinite(data->rotation[i]) || !isfinite(data->projection[i])) return 0;
    return data->projection[0]>0 && data->projection[5]>0 && fabsf(data->projection[11]-1)<.001f && fabsf(data->projection[15])<.001f;
}
static int render_projection_begin(int width,int height,float camera[6]) {
    rp_frame.valid=0;rp_frame.stage=1;
#if defined(__ANDROID__) && defined(__aarch64__)
    if(width<1 || height<1 || !camera_frame_valid || !local_player_controlled) return 0;
    rp_frame.stage=2;
    const uint32_t expected[]={0xf9404d01,0xf9416128,0xd63f0100};uint32_t code[3];
    if(!ev_read(ue4_base+0x115d43f4,code,sizeof(code)) || memcmp(code,expected,sizeof(code))) return 0;
    uint64_t manager=resolved_camera_owner,controller=ev_ptr(manager+0x370),local=ev_ptr(controller+0x3e8);
    uint64_t client=ev_ptr(local+0x70),viewport=ev_ptr(client+0x98);
    EVObject player,owner,viewport_client;
    rp_frame.stage=3;
    if(!ev_object(controller,&owner) || !ev_object(local,&player) || !ev_object(client,&viewport_client) ||
        !ew_is_a(local,"LocalPlayer",0x78) || !ew_is_a(client,"GameViewportClient",0xa0) ||
        ev_ptr(local+0x30)!=controller || ev_ptr(controller+0x408)!=manager ||
        !is_any_ptr(viewport) || ev_ptr(viewport)<ue4_base || ev_ptr(viewport)>=ue4_end) return 0;
    uint64_t method=ev_ptr(ev_ptr(local)+0x2c0);
    rp_frame.stage=4;
    static int method_logged;
    if(!method_logged) {char text[110];snprintf(text,sizeof(text),"projection method: rva=%lx",(unsigned long)(method-ue4_base));_diag(text);method_logged=1;}
    if(method<ue4_base+0x7fc9e60 || method>=ue4_base+0x1bfba790) return 0;
    if(method!=ue4_base+0x109f1c20) return 0;
    unsigned char output[0x200] __attribute__((aligned(16)))={0};
    typedef uint8_t (*GetProjectionData)(void *,void *,int,void *);
    rp_frame.stage=5;
    if(!((GetProjectionData)(uintptr_t)method)((void *)(uintptr_t)local,(void *)(uintptr_t)viewport,0,output) ||
        !ev_same(&owner) || !ev_same(&player) || !ev_same(&viewport_client)) return 0;
    rp_frame.stage=6;
    RPData data;memcpy(&data,output,sizeof(data));if(!rp_valid_data(&data)) return 0;
    rp_frame.data=data;rp_frame.scale_x=(float)width/data.view_rect[2];rp_frame.scale_y=(float)height/data.view_rect[3];rp_frame.valid=1;rp_frame.stage=0;
    for(int j=0;j<3;j++) camera[j]=data.origin[j]/100;
    camera[5]=2*atanf(1/data.projection[0])*(float)(180/M_PI);
    static int64_t next_report;
    if(ef_now()>=next_report) {
        float max_error=0;int comparisons=0;
        const uint32_t core_expected[]={0xd10643ff,0xa91467fc,0xa9155ff8};uint32_t core_code[3];
        if(ev_read(ue4_base+0x115d4360,core_code,sizeof(core_code)) && !memcmp(core_code,core_expected,sizeof(core_code))) {
            typedef uint8_t (*ProjectCore)(void *,const float *,float *,int);
            for(int sample=0;sample<3;sample++) {
                float point[3];
                for(int j=0;j<3;j++) point[j]=data.origin[j]+data.rotation[j*4+2]*1000+(sample==1?data.rotation[j*4]*100:sample==2?data.rotation[j*4+1]*100:0);
                float native[2],ours[2];
                if(((ProjectCore)(uintptr_t)(ue4_base+0x115d4360))((void *)(uintptr_t)controller,point,native,0) &&
                    render_projection_point(point[0]/100,point[1]/100,point[2]/100,&ours[0],&ours[1])) {
                    float error=hypotf(ours[0]-native[0]*rp_frame.scale_x,ours[1]-native[1]*rp_frame.scale_y);
                    ++comparisons;
                    if(error>max_error) max_error=error;
                }
            }
        }
        char text[200];snprintf(text,sizeof(text),"projection: viewport=%dx%d rect=%d,%d,%d,%d scale=%.3f,%.3f hfov=%.2f near=%.2f",
            data.view_rect[2],data.view_rect[3],data.rect[0],data.rect[1],data.rect[2],data.rect[3],data.projection[0],data.projection[5],camera[5],data.projection[14]);_diag(text);next_report=ef_now()+5000;
        snprintf(text,sizeof(text),"projection comparison: samples=%d native_max_error_px=%.3f",comparisons,max_error);_diag(text);
    }
    return 1;
#else
    (void)width;(void)height;(void)camera;return 0;
#endif
}
/* -1 means no validated matrix; 0 means behind/invalid, 1 means projected.
 * Unreal uses row vectors and centimeters in this captured implementation. */
static int render_projection_point(float x,float y,float z,float *screen_x,float *screen_y) {
    if(!rp_frame.valid) return -1;
    const RPData *d=&rp_frame.data;
    float world[]={x*100-d->origin[0],y*100-d->origin[1],z*100-d->origin[2],1};
    float view[4]={0},clip[4]={0};
    for(int column=0;column<4;column++) for(int row=0;row<4;row++) view[column]+=world[row]*d->rotation[row*4+column];
    for(int column=0;column<4;column++) for(int row=0;row<4;row++) clip[column]+=view[row]*d->projection[row*4+column];
    if(!isfinite(clip[3]) || clip[3]<=0) return 0;
    float nx=clip[0]/clip[3],ny=clip[1]/clip[3];
    *screen_x=(d->rect[0]+(.5f+.5f*nx)*(d->rect[2]-d->rect[0]))*rp_frame.scale_x;
    *screen_y=(d->rect[1]+(.5f-.5f*ny)*(d->rect[3]-d->rect[1]))*rp_frame.scale_y;
    return isfinite(*screen_x) && isfinite(*screen_y);
}
#endif
