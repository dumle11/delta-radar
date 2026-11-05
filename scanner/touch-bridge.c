/* Owns a virtual touchscreen while forwarding physical contacts unchanged.
 * A separate slot supplies camera motion; no Android shell swipe gestures. */
#define _GNU_SOURCE
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#define PHYSICAL_SLOTS 10
#define AIM_SLOT 10
#define FIRE_SLOT 11
#define SLOTS 12
static volatile sig_atomic_t running=1;
static int physical=-1, virtual_fd=-1, grabbed=0, current_slot=0;
static int raw_xmax=1079,raw_ymax=1919,next_id=1;
static long long last_command, created_at;
static struct Contact {int active,id,out_slot,x,y,major,minor,start_x,start_y,start_valid,look_moved;} contacts[SLOTS];
static long long milliseconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
static void stop(int sig){(void)sig;running=0;}
static int emit(int type,int code,int value){struct input_event e={0};e.type=type;e.code=code;e.value=value;if(write(virtual_fd,&e,sizeof(e))!=sizeof(e)){running=0;return 0;}return 1;}
static int allocate_id(void){for(int n=0;n<65535;n++){int id=next_id++;if(next_id>65535)next_id=1;int used=0;for(int i=0;i<SLOTS;i++)if(contacts[i].active&&contacts[i].id==id)used=1;if(!used)return id;}return 1;}
/* Append contacts rather than sorting synthetic/physical fingers by source
 * slot: inserting a lower slot changes an existing Android pointer's index. */
static int allocate_slot(void) {
    int highest=-1, used[SLOTS]={0};
    for (int i=0;i<SLOTS;i++) if (contacts[i].active) {
        used[contacts[i].out_slot]=1;
        if (contacts[i].out_slot>highest) highest=contacts[i].out_slot;
    }
    if (highest+1<SLOTS) return highest+1;
    for (int i=0;i<SLOTS;i++) if (!used[i]) return i;
    return -1;
}
static void release_slot(int slot) {
    if(!contacts[slot].active)return;
    emit(EV_ABS,ABS_MT_SLOT,contacts[slot].out_slot);
    emit(EV_ABS,ABS_MT_TRACKING_ID,-1);contacts[slot].active=0;
}
static void sync_frame(void){int any=0;for(int i=0;i<SLOTS;i++)any|=contacts[i].active;emit(EV_KEY,BTN_TOOL_FINGER,any);emit(EV_KEY,BTN_TOUCH,any);emit(EV_SYN,SYN_REPORT,0);}
static void position_slot(int slot,int x,int y) {
    struct Contact *c=&contacts[slot];
    if(!c->active) {
        c->out_slot=allocate_slot();if(c->out_slot<0){running=0;return;}
        c->id=allocate_id();c->active=1;
        emit(EV_ABS,ABS_MT_SLOT,c->out_slot);emit(EV_ABS,ABS_MT_TRACKING_ID,c->id);
        emit(EV_ABS,ABS_MT_TOUCH_MAJOR,8);emit(EV_ABS,ABS_MT_TOUCH_MINOR,8);
    } else emit(EV_ABS,ABS_MT_SLOT,c->out_slot);
    c->x=x;c->y=y;emit(EV_ABS,ABS_MT_POSITION_X,x);emit(EV_ABS,ABS_MT_POSITION_Y,y);
}
static int physical_count(void){int n=0;for(int i=0;i<PHYSICAL_SLOTS;i++)n+=contacts[i].active;return n;}
static void cleanup(void){if(virtual_fd>=0){for(int i=0;i<SLOTS;i++)release_slot(i);sync_frame();ioctl(virtual_fd,UI_DEV_DESTROY);close(virtual_fd);virtual_fd=-1;}if(physical>=0){if(grabbed)ioctl(physical,EVIOCGRAB,0);close(physical);physical=-1;}}
static int all_released(void){int values[PHYSICAL_SLOTS+1]={ABS_MT_TRACKING_ID};if(ioctl(physical,EVIOCGMTSLOTS(sizeof(values)),values)<0)return 0;for(int i=1;i<=PHYSICAL_SLOTS;i++)if(values[i]>=0)return 0;return 1;}
static int setup(void){
 physical=open("/dev/input/event4",O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(physical<0)return 0;
 char name[128]={0};struct input_absinfo ax,ay,slots;
 if(ioctl(physical,EVIOCGNAME(sizeof(name)),name)<0||strcmp(name,"synaptics,s3320")||ioctl(physical,EVIOCGABS(ABS_MT_POSITION_X),&ax)<0||ioctl(physical,EVIOCGABS(ABS_MT_POSITION_Y),&ay)<0||ioctl(physical,EVIOCGABS(ABS_MT_SLOT),&slots)<0||slots.maximum!=9)return 0;
 raw_xmax=ax.maximum;raw_ymax=ay.maximum;
 virtual_fd=open("/dev/uinput",O_WRONLY|O_NONBLOCK|O_CLOEXEC);if(virtual_fd<0)return 0;
 if(ioctl(virtual_fd,UI_SET_EVBIT,EV_SYN)<0||ioctl(virtual_fd,UI_SET_EVBIT,EV_KEY)<0||ioctl(virtual_fd,UI_SET_EVBIT,EV_ABS)<0||ioctl(virtual_fd,UI_SET_PROPBIT,INPUT_PROP_DIRECT)<0)return 0;
 int keys[]={BTN_TOUCH,BTN_TOOL_FINGER,KEY_BACK,KEY_APPSELECT,KEY_F4};for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);i++)if(ioctl(virtual_fd,UI_SET_KEYBIT,keys[i])<0)return 0;
 int axes[]={ABS_MT_SLOT,ABS_MT_TRACKING_ID,ABS_MT_POSITION_X,ABS_MT_POSITION_Y,ABS_MT_TOUCH_MAJOR,ABS_MT_TOUCH_MINOR};for(unsigned i=0;i<sizeof(axes)/sizeof(*axes);i++)if(ioctl(virtual_fd,UI_SET_ABSBIT,axes[i])<0)return 0;
 struct uinput_user_dev dev={0};snprintf(dev.name,sizeof(dev.name),"Delta Touch Bridge");dev.id.bustype=BUS_VIRTUAL;dev.id.vendor=0x18d1;dev.id.product=0xd17a;dev.id.version=1;
 dev.absmax[ABS_MT_SLOT]=SLOTS-1;dev.absmin[ABS_MT_TRACKING_ID]=0;dev.absmax[ABS_MT_TRACKING_ID]=65535;dev.absmax[ABS_MT_POSITION_X]=raw_xmax;dev.absmax[ABS_MT_POSITION_Y]=raw_ymax;dev.absmax[ABS_MT_TOUCH_MAJOR]=255;dev.absmax[ABS_MT_TOUCH_MINOR]=255;
 if(write(virtual_fd,&dev,sizeof(dev))!=sizeof(dev)||ioctl(virtual_fd,UI_DEV_CREATE)<0)return 0;
 return 1;
}
static void physical_event(const struct input_event *e) {
    if (e->type == EV_ABS) {
        if (e->code == ABS_MT_SLOT) { current_slot = e->value; return; }
        if (current_slot < 0 || current_slot >= PHYSICAL_SLOTS) return;
        struct Contact *c = &contacts[current_slot];
        if (e->code == ABS_MT_TRACKING_ID) {
            if (e->value < 0) release_slot(current_slot);
            else {
                c->out_slot = allocate_slot(); if(c->out_slot<0){running=0;return;}
                c->id = allocate_id(); c->active = 1; c->start_valid = 0; c->look_moved = 0;
                emit(EV_ABS, ABS_MT_SLOT, c->out_slot);
                emit(EV_ABS, ABS_MT_TRACKING_ID, c->id);
            }
            return;
        }
        if (e->code == ABS_MT_POSITION_X) { c->x = e->value; }
        if (e->code == ABS_MT_POSITION_Y) { c->y = e->value; }
        if (!c->active) return;
        emit(EV_ABS, ABS_MT_SLOT, c->out_slot);
        if (c->start_valid && abs(c->x-c->start_x)+abs(c->y-c->start_y)>8) c->look_moved=1;
        if (e->code==ABS_MT_POSITION_X || e->code==ABS_MT_POSITION_Y ||
            e->code==ABS_MT_TOUCH_MAJOR || e->code==ABS_MT_TOUCH_MINOR) emit(e->type,e->code,e->value);
    } else if (e->type == EV_SYN) {
        if (e->code == SYN_REPORT) {
            for (int i=0; i<PHYSICAL_SLOTS; i++) {
                if (!contacts[i].active || contacts[i].start_valid) continue;
                /* Drivers omit unchanged axes on a new tracking ID. Cached
                 * source coordinates are seeded when grabbing, then updated
                 * only by the source event stream. Do not replace them with
                 * a later ioctl snapshot from a different report. */
                emit(EV_ABS,ABS_MT_SLOT,contacts[i].out_slot);
                emit(EV_ABS,ABS_MT_POSITION_X,contacts[i].x);
                emit(EV_ABS,ABS_MT_POSITION_Y,contacts[i].y);
                emit(EV_ABS,ABS_MT_TOUCH_MAJOR,8); emit(EV_ABS,ABS_MT_TOUCH_MINOR,8);
                contacts[i].start_x=contacts[i].x; contacts[i].start_y=contacts[i].y;
                contacts[i].start_valid=1;
            }
            sync_frame();
        } else if (e->code == SYN_DROPPED) {
            for (int i=0;i<SLOTS;i++) release_slot(i);
            sync_frame(); running=0;
        }
    } else if (e->type==EV_KEY && e->code!=BTN_TOOL_FINGER && e->code!=BTN_TOUCH) {
        emit(e->type,e->code,e->value);
    }
}
static int clamp(int n,int lo,int hi){return n<lo?lo:n>hi?hi:n;}
static void screen_to_raw(float sx,float sy,int width,int height,int rotation,int *x,int *y){float nx=sx/(width-1),ny=sy/(height-1);float rx=nx,ry=ny;if(rotation==1){rx=1-ny;ry=nx;}else if(rotation==2){rx=1-nx;ry=1-ny;}else if(rotation==3){rx=ny;ry=1-nx;}*x=clamp(lroundf(rx*raw_xmax),0,raw_xmax);*y=clamp(lroundf(ry*raw_ymax),0,raw_ymax);}
static void command(char *line){int seq,w,h,rot,active,fire;float ax,ay,dx,dy,fx,fy;
 if(!strncmp(line,"QUIT",4)){running=0;return;}
 if(sscanf(line,"STEP %d %d %d %d %f %f %f %f %d %f %f %d",&seq,&w,&h,&rot,&ax,&ay,&dx,&dy,&active,&fx,&fy,&fire)!=12||w<2||h<2||rot<0||rot>3||!isfinite(ax)||!isfinite(ay)||!isfinite(dx)||!isfinite(dy)||!isfinite(fx)||!isfinite(fy)){puts("ERROR protocol");fflush(stdout);return;}
 last_command=milliseconds();
 if(!grabbed&&milliseconds()-created_at>=400&&all_released()){if(ioctl(physical,EVIOCGRAB,1)==0){
     grabbed=1;
     int xs[PHYSICAL_SLOTS+1]={ABS_MT_POSITION_X},ys[PHYSICAL_SLOTS+1]={ABS_MT_POSITION_Y};
     if(ioctl(physical,EVIOCGMTSLOTS(sizeof(xs)),xs)<0||ioctl(physical,EVIOCGMTSLOTS(sizeof(ys)),ys)<0){running=0;return;}
     for(int i=0;i<PHYSICAL_SLOTS;i++){contacts[i].x=xs[i+1];contacts[i].y=ys[i+1];}
 }}
 static float sx,sy;
 int applied=contacts[AIM_SLOT].active;
 int manual_look=0;
 for(int i=0;i<PHYSICAL_SLOTS;i++)if(contacts[i].active){
     struct Contact *c=&contacts[i];
     int startx=c->start_valid?c->start_x:c->x, starty=c->start_valid?c->start_y:c->y;
     float screenx=rot==1?(float)starty/raw_ymax*w:
         rot==3?(1-(float)starty/raw_ymax)*w:
         rot==2?(1-(float)startx/raw_xmax)*w:(float)startx/raw_xmax*w;
     if(screenx>.55f*w&&c->look_moved)manual_look=1;
 }

 if(!grabbed||!active||manual_look){release_slot(AIM_SLOT);applied=0;}else {
 if(!contacts[AIM_SLOT].active){sx=ax;sy=ay;}else {sx+=dx;sy+=dy;}
 if(sx<.52f*w||sx>.99f*w||sy<.01f*h||sy>.99f*h){release_slot(AIM_SLOT);sync_frame();sx=ax;sy=ay;applied=0;}
 int x,y;screen_to_raw(sx,sy,w,h,rot,&x,&y);position_slot(AIM_SLOT,x,y);
 }
 if(grabbed&&fire){int x,y;screen_to_raw(fx,fy,w,h,rot,&x,&y);position_slot(FIRE_SLOT,x,y);}else release_slot(FIRE_SLOT);
 sync_frame();printf("ACK %d %d %d %d %d\n",seq,grabbed,physical_count(),manual_look,applied);fflush(stdout);
}
int main(void){signal(SIGINT,stop);signal(SIGTERM,stop);signal(SIGHUP,stop);signal(SIGPIPE,stop);atexit(cleanup);setvbuf(stdout,NULL,_IOLBF,0);if(!setup()){fprintf(stdout,"ERROR setup %d\n",errno);return 1;}puts("READY");last_command=created_at=milliseconds();
 char buffer[1024];size_t used=0;
 while(running){struct pollfd fds[]={{STDIN_FILENO,POLLIN,0},{physical,POLLIN,0}};int result=poll(fds,2,16);if(result<0&&errno!=EINTR)break;
 if(fds[0].revents&(POLLHUP|POLLERR))break;
 if(fds[0].revents&POLLIN){ssize_t n=read(STDIN_FILENO,buffer+used,sizeof(buffer)-used-1);if(n<=0)break;used+=(size_t)n;buffer[used]=0;char *start=buffer,*end;while((end=strchr(start,'\n'))){*end=0;command(start);start=end+1;}used-= (size_t)(start-buffer);memmove(buffer,start,used);if(used>=sizeof(buffer)-1)break;}
 if(fds[1].revents&POLLIN){struct input_event es[64];ssize_t n;while((n=read(physical,es,sizeof(es)))>0)if(grabbed){for(int i=0;i<n/(int)sizeof(*es);i++)physical_event(&es[i]);}}
 if(milliseconds()-last_command>150){release_slot(AIM_SLOT);release_slot(FIRE_SLOT);sync_frame();}
 if(milliseconds()-last_command>2500)break;
 }
 return 0;
}
