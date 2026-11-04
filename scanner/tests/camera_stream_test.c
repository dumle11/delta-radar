#define _GNU_SOURCE
#define __ANDROID_API__ 31
#define CAMERA_STREAM_SOCKET_NAME "delta_owned_integration_camera_test"
#define main scanner_program_main
#include "../memscan.c"
#undef main
#include <assert.h>
static void number(unsigned char *p,float value){memcpy(p,&value,4);}
static void packet(int client,char *out,size_t size) {
    size_t n=0;char ch;
    while(n+1<size && recv(client,&ch,1,0)==1) {out[n++]=ch;if(ch=='\n')break;}
    out[n]=0;assert(n>0 && out[n-1]=='\n');
}
int main(void) {
    int server=socket(AF_UNIX,SOCK_STREAM,0);assert(server>=0);
    struct sockaddr_un address={.sun_family=AF_UNIX};
    memcpy(address.sun_path+1,CAMERA_STREAM_SOCKET_NAME,strlen(CAMERA_STREAM_SOCKET_NAME));
    assert(bind(server,(struct sockaddr *)&address,offsetof(struct sockaddr_un,sun_path)+1+strlen(CAMERA_STREAM_SOCKET_NAME))==0);
    assert(listen(server,1)==0);
    unsigned char cache[48]={0};number(cache,1);number(cache+16,1000);number(cache+20,2000);number(cache+24,3000);
    cache[28]=cache[29]=255;number(cache+44,90);
    char lease[]="/tmp/delta-owned-camera-lease-XXXXXX";
    int lease_fd=mkstemp(lease);assert(lease_fd>=0);
    assert(write(lease_fd,"active\n",7)==7);close(lease_fd);
    session_file=lease;session_owner_pid=getpid();
    running=1;camera_stream_publish(getpid(),0,0);camera_stream_start();
    int client=accept(server,NULL,NULL);assert(client>=0);
    struct timeval timeout={.tv_sec=2};assert(setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))==0);
    int initial=0,rotated=0,stale=0;char line[1024];
    packet(client,line,sizeof(line));
    assert(strstr(line,"\"world\":\"0x0\"") && strstr(line,"\"valid\":false"));
    camera_stream_publish(getpid(),0x1234,(uintptr_t)cache);
    for(int i=0;i<80;i++) {
        packet(client,line,sizeof(line));
        assert(strstr(line,"\"sampledElapsedMs\":") && strstr(line,"\"world\":\"0x1234\""));
        if(strstr(line,"\"valid\":true") && strstr(line,"\"x\":10.000000"))initial=1;
        if(initial && !rotated) {number(cache+36,90);number(cache,2);}
        if(strstr(line,"\"yaw\":90.000000") && strstr(line,"\"valid\":true"))rotated=1;
        if(rotated && strstr(line,"\"valid\":false")){stale=1;break;}
    }
    assert(initial&&rotated&&stale);
    camera_stream_publish(getpid(),0x5678,0);
    int cleared=0;
    for(int i=0;i<20;i++) {packet(client,line,sizeof(line));if(strstr(line,"\"world\":\"0x5678\"")&&strstr(line,"\"valid\":false")){cleared=1;break;}}
    assert(cleared);
    long long stopped_at=sample_time_ms();assert(unlink(lease)==0);
    camera_stream_stop();assert(!running && sample_time_ms()-stopped_at<1000);
    close(client);close(server);
    puts("Owned camera socket: monotonic samples, rotation, freeze expiry, source invalidation and lease cancellation passed");
}
