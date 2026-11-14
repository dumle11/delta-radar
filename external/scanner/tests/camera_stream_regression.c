#define CAMERA_STREAM_SOCKET_NAME "delta_tracking_owned_camera_test"
#define main scanner_main
#include "../memscan-legacy.c"
#undef main
#include <assert.h>
static void number(unsigned char *p,float value){memcpy(p,&value,4);}
int main(void){
    int server=socket(AF_UNIX,SOCK_STREAM,0);assert(server>=0);
    struct sockaddr_un address={.sun_family=AF_UNIX};memcpy(address.sun_path+1,CAMERA_STREAM_SOCKET_NAME,strlen(CAMERA_STREAM_SOCKET_NAME));
    assert(bind(server,(struct sockaddr*)&address,offsetof(struct sockaddr_un,sun_path)+1+strlen(CAMERA_STREAM_SOCKET_NAME))==0);assert(listen(server,1)==0);
    unsigned char cache[48]={0};number(cache,1);number(cache+16,1000);number(cache+20,2000);number(cache+24,3000);
    cache[28]=cache[29]=255;number(cache+44,90);
    game_pid=getpid();atomic_store(&camera_stream_cache,(uintptr_t)cache);atomic_store(&camera_stream_world,0x1234);camera_stream_start();
    int client=accept(server,NULL,NULL);assert(client>=0);
    struct timeval timeout={.tv_sec=2};setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    char packet[1024];int saw_initial=0,saw_rotated=0,saw_stale=0;
    for(int i=0;i<80;i++){
        size_t n=0;char ch;while(n+1<sizeof(packet)&&recv(client,&ch,1,0)==1){packet[n++]=ch;if(ch=='\n')break;}packet[n]=0;assert(n>0);
        if(strstr(packet,"\"valid\":true")&&strstr(packet,"\"x\":10.000000")&&strstr(packet,"\"world\":\"0x1234\"")){saw_initial=1;}
        if(saw_initial&&!saw_rotated){number(cache+36,90);number(cache,2);}
        if(strstr(packet,"\"yaw\":90.000000")&&strstr(packet,"\"valid\":true"))saw_rotated=1;
        if(saw_rotated&&strstr(packet,"\"valid\":false")){saw_stale=1;break;}
    }
    atomic_store(&running,0);close(client);close(server);
    assert(saw_initial&&saw_rotated&&saw_stale);
    puts("owned camera stream: coherent coordinates, rotation updates and frozen-frame expiry passed");return 0;
}
