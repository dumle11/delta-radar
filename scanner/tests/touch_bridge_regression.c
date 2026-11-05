#define main touch_bridge_main
#include "../touch-bridge.c"
#undef main
#include <assert.h>
int main(void){
 int x,y;screen_to_raw(0,0,1920,1080,1,&x,&y);assert(x==1079&&y==0);
 screen_to_raw(1919,1079,1920,1080,1,&x,&y);assert(x==0&&y==1919);
 screen_to_raw(0,0,1920,1080,3,&x,&y);assert(x==0&&y==1919);
 screen_to_raw(1919,1079,1920,1080,3,&x,&y);assert(x==1079&&y==0);
 int pipefd[2];assert(pipe(pipefd)==0);virtual_fd=pipefd[1];grabbed=1;
 struct input_event es[]={ {.type=EV_ABS,.code=ABS_MT_SLOT,.value=0},
 {.type=EV_ABS,.code=ABS_MT_TRACKING_ID,.value=65000},
 {.type=EV_ABS,.code=ABS_MT_POSITION_X,.value=600},
 {.type=EV_ABS,.code=ABS_MT_POSITION_Y,.value=200},
 {.type=EV_SYN,.code=SYN_REPORT,.value=0} };
 for(unsigned i=0;i<sizeof(es)/sizeof(*es);i++)physical_event(&es[i]);
 assert(contacts[0].active&&contacts[0].x==600&&contacts[0].y==200);
 int physical_id=contacts[0].id;
 char start[]="STEP 1 1920 1080 1 1400 400 0 0 1 0 0 0";command(start);
 assert(contacts[AIM_SLOT].active&&contacts[0].active);
 assert(contacts[AIM_SLOT].id!=physical_id);
 int before=contacts[AIM_SLOT].y;
 char move[]="STEP 2 1920 1080 1 1400 400 100 0 1 0 0 0";command(move);
 assert(contacts[AIM_SLOT].y>before&&contacts[0].id==physical_id);
 char release[]="STEP 3 1920 1080 1 1400 400 0 0 0 0 0 0";command(release);
 assert(!contacts[AIM_SLOT].active&&contacts[0].active&&contacts[0].id==physical_id);
 /* A manual camera finger gets priority without cancelling the walking finger. */
 es[0].value=1;es[3].value=1600;
 for(unsigned i=0;i<sizeof(es)/sizeof(*es);i++)physical_event(&es[i]);
 struct input_event motion={.type=EV_ABS,.code=ABS_MT_POSITION_Y,.value=1620};physical_event(&motion);
 char manual[]="STEP 4 1920 1080 1 1400 400 20 0 1 0 0 0";command(manual);
 assert(!contacts[AIM_SLOT].active&&contacts[0].active&&contacts[1].active);
 /* Crossing the centre never changes which finger owns manual camera control. */
 motion.value=100;physical_event(&motion);
 char crossing[]="STEP 41 1920 1080 1 1400 400 20 0 1 0 0 0";command(crossing);
 assert(!contacts[AIM_SLOT].active&&contacts[0].active&&contacts[1].active);
 /* Aim first, then walking: new physical contacts append after the camera
  * pointer, irrespective of the physical device's original slot number. */
 for(int i=0;i<SLOTS;i++)release_slot(i);
 memset(contacts,0,sizeof(contacts));
 char aimfirst[]="STEP 5 1920 1080 1 1400 400 0 0 1 0 0 0";command(aimfirst);
 int camera_slot=contacts[AIM_SLOT].out_slot;
 es[0].value=0;es[3].value=200;
 for(unsigned i=0;i<sizeof(es)/sizeof(*es);i++)physical_event(&es[i]);
 assert(contacts[AIM_SLOT].out_slot==camera_slot);
 assert(contacts[0].out_slot>camera_slot);
 int walking_slot=contacts[0].out_slot;
 release_slot(AIM_SLOT);position_slot(AIM_SLOT,500,1000);
 assert(contacts[0].out_slot==walking_slot);
 assert(contacts[AIM_SLOT].out_slot>walking_slot);
 /* A new contact with omitted axes retains the last source coordinates. */
 release_slot(0);
 current_slot=0;
 struct input_event birth={.type=EV_ABS,.code=ABS_MT_TRACKING_ID,.value=1234};
 physical_event(&birth);
 struct input_event syn={.type=EV_SYN,.code=SYN_REPORT};physical_event(&syn);
 assert(contacts[0].x==600&&contacts[0].y==200);
 /* Coordinates after an UP must never move the active synthetic contact. */
 release_slot(0);
 fcntl(pipefd[0],F_SETFL,O_NONBLOCK);
 struct input_event drained[128];while(read(pipefd[0],drained,sizeof(drained))>0){}
 struct input_event inactive={.type=EV_ABS,.code=ABS_MT_POSITION_X,.value=777};
 physical_event(&inactive);
 assert(read(pipefd[0],drained,sizeof(drained))<0&&errno==EAGAIN);
 close(pipefd[0]);close(pipefd[1]);virtual_fd=-1;physical=-1;grabbed=0;
 puts("touch bridge: rotations, separate contacts, walking preservation, manual priority passed");return 0;
}
