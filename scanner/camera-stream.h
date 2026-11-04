#ifndef DELTA_CAMERA_STREAM_H
#define DELTA_CAMERA_STREAM_H
#ifndef CAMERA_STREAM_SOCKET_NAME
#define CAMERA_STREAM_SOCKET_NAME "delta_radar_camera"
#endif
/* Dedicated camera channel cannot queue behind actor discovery or JSON poses.
 * This thread uses only process_vm_readv and private state, not reader globals
 * that collect per-actor diagnostics. */
static int camera_stream_read(uint64_t address, void *out, size_t size) {
    struct iovec local={.iov_base=out,.iov_len=size},remote={.iov_base=(void*)address,.iov_len=size};
    return syscall(__NR_process_vm_readv,game_pid,&local,1,&remote,1,0)==(ssize_t)size;
}
static int camera_stream_connect(void) {
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(fd<0)return -1;
    struct sockaddr_un a={.sun_family=AF_UNIX};const char *name=CAMERA_STREAM_SOCKET_NAME;
    memcpy(a.sun_path+1,name,strlen(name));
    struct timeval timeout={.tv_sec=0,.tv_usec=20000};setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    if(connect(fd,(struct sockaddr*)&a,(socklen_t)(offsetof(struct sockaddr_un,sun_path)+1+strlen(name)))<0){close(fd);return -1;}
    return fd;
}
static void *camera_stream_worker(void *unused) {
    (void)unused;int fd=-1;float previous=-1;uint64_t old_cache=0;long long changed_at=0,retry_at=0,last_sent=0;
    while(running){
        long long now=sample_time_ms();
        if(fd<0&&now>=retry_at){fd=camera_stream_connect();retry_at=now+250;}
        unsigned version=atomic_load(&camera_stream_version);uint64_t cache=atomic_load(&camera_stream_cache),world=atomic_load(&camera_stream_world);
        unsigned char a[48],b[48];int valid=0;float stamp=0,xyz[3]={0},angles[3]={0},fov=0;
        if(!(version&1)&&cache&&world&&camera_stream_read(cache,a,sizeof(a))&&camera_stream_read(cache,b,sizeof(b))&&
            !memcmp(a,b,sizeof(a))&&version==atomic_load(&camera_stream_version)){
            memcpy(&stamp,a,4);memcpy(xyz,a+16,12);memcpy(angles,a+32,12);memcpy(&fov,a+44,4);
            valid=enc_handler_unmanaged(a+28)&&isfinite(stamp)&&stamp>0&&isfinite(fov)&&fov>=5&&fov<=150;
            for(int i=0;i<3;i++)valid=valid&&isfinite(xyz[i])&&fabsf(xyz[i])<10000000&&isfinite(angles[i]);
            if(stamp!=previous||cache!=old_cache){previous=stamp;old_cache=cache;changed_at=now;}
            valid=valid&&now-changed_at<=100;
        }
        if(fd>=0&&(now-last_sent>=8||!valid)){
            char packet[512];int n=snprintf(packet,sizeof(packet),"{\"gamePid\":%d,\"world\":\"0x%llx\",\"sampledAt\":%lld,\"timestamp\":%.9g,\"valid\":%s,\"x\":%.6f,\"y\":%.6f,\"z\":%.6f,\"pitch\":%.6f,\"yaw\":%.6f,\"roll\":%.6f,\"fov\":%.6f}\n",game_pid,(unsigned long long)world,(valid?changed_at:now),(double)stamp,valid?"true":"false",xyz[0]/100,xyz[1]/100,xyz[2]/100,angles[0],angles[1],angles[2],fov);
            if(n<=0||n>=(int)sizeof(packet)||send(fd,packet,(size_t)n,MSG_NOSIGNAL)!=n){close(fd);fd=-1;}
            last_sent=now;
        }
        usleep(4000);
    }
    if(fd>=0)close(fd);return NULL;
}
static void camera_stream_start(void) {
    pthread_t thread;if(!pthread_create(&thread,NULL,camera_stream_worker,NULL))pthread_detach(thread);
}
#endif
