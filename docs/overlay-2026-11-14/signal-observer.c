#define _GNU_SOURCE
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <dirent.h>
#include <elf.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define MAX_THREADS 2048
static int tids[MAX_THREADS], count;
static void remember(int tid) { for(int i=0;i<count;i++)if(tids[i]==tid)return; if(count<MAX_THREADS)tids[count++]=tid; }
static long seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec;}
static void mapping(int tid,uint64_t address){
 char path[80],line[1024];snprintf(path,sizeof(path),"/proc/%d/maps",tid);
 FILE*f=fopen(path,"r");if(!f)return;
 unsigned long long start,end;
 while(fgets(line,sizeof(line),f)){
  if(sscanf(line,"%llx-%llx",&start,&end)==2&&address>=start&&address<end){
   printf("MAP address=0x%llx %s",(unsigned long long)address,line);break;
  }
 }
 fclose(f);
}
static int word(int tid,uint64_t address,uint64_t*out){
 errno=0;long value=ptrace(PTRACE_PEEKDATA,tid,(void*)(uintptr_t)address,0);
 if(value==-1&&errno)return 0;*out=(uint64_t)value;return 1;
}
static void report(int tid,int sig){
 siginfo_t info;memset(&info,0,sizeof(info));
 if(ptrace(PTRACE_GETSIGINFO,tid,0,&info)<0){perror("getsiginfo");return;}
 uint64_t registers[34]={0};struct iovec io={registers,sizeof(registers)};
 ptrace(PTRACE_GETREGSET,tid,(void*)NT_PRSTATUS,&io);
 char path[80],name[80]="?";snprintf(path,sizeof(path),"/proc/%d/comm",tid);
 FILE*f=fopen(path,"r");if(f){fgets(name,sizeof(name),f);fclose(f);name[strcspn(name,"\n")]=0;}
 printf("SIGNAL tid=%d name=%s signal=%d code=%d sender_pid=%d sender_uid=%u address=%p pc=0x%llx lr=0x%llx\n",
 tid,name,sig,info.si_code,info.si_pid,info.si_uid,info.si_addr,(unsigned long long)registers[32],(unsigned long long)registers[30]);
 mapping(tid,registers[32]);mapping(tid,registers[30]);
 uint64_t fp=registers[29];
 for(int depth=0;depth<24&&fp&&!(fp&15);depth++){
  uint64_t next,ret;if(!word(tid,fp,&next)||!word(tid,fp+8,&ret))break;
  printf("FRAME depth=%d fp=0x%llx return=0x%llx\n",depth,(unsigned long long)fp,(unsigned long long)ret);
  mapping(tid,ret);
  if(next<=fp||next-fp>1024*1024)break;fp=next;
 }

}
int main(int argc,char**argv){
 setbuf(stdout,0);if(argc<2)return 2;int pid=atoi(argv[1]);long duration=argc>2?atol(argv[2]):100;
 char path[80];snprintf(path,sizeof(path),"/proc/%d/task",pid);DIR*d=opendir(path);if(!d){perror("task");return 3;}
 struct dirent*entry;int admitted=0;
 while((entry=readdir(d))){int tid=atoi(entry->d_name);if(tid<=0)continue;
  if(ptrace(PTRACE_SEIZE,tid,0,(void*)(PTRACE_O_TRACECLONE|PTRACE_O_TRACEEXIT))==0){remember(tid);admitted++;}
 }
 closedir(d);printf("observing pid=%d threads=%d seconds=%ld; original signals will be forwarded\n",pid,admitted,duration);
 if(!admitted)return 4;
 long deadline=seconds()+duration;
 while(seconds()<deadline){
  int status;int tid=waitpid(-1,&status,__WALL|WNOHANG);
  if(tid==0){if(kill(pid,0)<0&&errno==ESRCH)break;usleep(10000);continue;}
  if(tid<0){if(errno==EINTR)continue;break;}
  remember(tid);
  if(WIFEXITED(status)||WIFSIGNALED(status)){
   if(tid==pid){printf("target exit: exited=%d code=%d signal=%d\n",WIFEXITED(status),WIFEXITED(status)?WEXITSTATUS(status):0,WIFSIGNALED(status)?WTERMSIG(status):0);break;}
   continue;
  }
  if(!WIFSTOPPED(status))continue;
  int event=status>>16;int sig=WSTOPSIG(status);
  if(event==PTRACE_EVENT_CLONE){unsigned long child=0;ptrace(PTRACE_GETEVENTMSG,tid,0,&child);remember((int)child);}
  if(!event&&(sig==SIGBUS||sig==SIGSEGV||sig==SIGABRT||sig==SIGSYS))report(tid,sig);
  ptrace(PTRACE_CONT,tid,0,(void*)(intptr_t)(event?0:sig));
 }
 /* Detach safely: interrupt running tracees first, then resume with any real
  * pending signal. Never kill the target as part of observer cleanup. */
 int interrupted=0,detached=0;
 for(int i=0;i<count;i++)if(ptrace(PTRACE_INTERRUPT,tids[i],0,0)==0)interrupted++;
 long cleanup=seconds()+5;
 while(seconds()<cleanup&&detached<interrupted){
  int status;int tid=waitpid(-1,&status,__WALL|WNOHANG);
  if(tid==0){usleep(10000);continue;}if(tid<0)break;
  if(WIFSTOPPED(status)){
   int event=status>>16;int sig=event?0:WSTOPSIG(status);
   if(ptrace(PTRACE_DETACH,tid,0,(void*)(intptr_t)sig)==0)detached++;
  }
 }
 printf("cleanup: interrupted=%d detached=%d; target not killed\n",interrupted,detached);
 return detached<interrupted?5:0;
}
