#ifndef INTERNAL_GPU_VIEW_PROFILE_H
#define INTERNAL_GPU_VIEW_PROFILE_H
/* Reflect the exact packed-uniform matrix expression emitted by HLSLCC. No
 * names or fixed offsets: identify four vec4 rows multiplied by x/y/z/w of
 * one position, in each branch, and its preceding pre-view translation load. */
typedef struct { int valid,matrix_a,matrix_b,translation,local_origin; } GvProfile;
static GvProfile gv_profile(const uint32_t *words,size_t count) {
    GvProfile result={0};
    if(count<5 || words[0]!=0x07230203 || words[3]>65536) return result;
    uint32_t bound=words[3];
    uint32_t *constants=calloc(bound,sizeof(uint32_t));
    uint8_t *known=calloc(bound,1),*binding=calloc(bound,1),*set=calloc(bound,1);
    if(!constants || !known || !binding || !set) goto done;
    memset(binding,255,bound);memset(set,255,bound);
    for(size_t i=5;i<count;) {
        uint32_t n=words[i]>>16,op=words[i]&65535;
        if(!n || i+n>count) goto done;
        if(op==43 && n==4 && words[i+2]<bound) {constants[words[i+2]]=words[i+3];known[words[i+2]]=1;}
        if(op==71 && n==4 && words[i+1]<bound) {
            if(words[i+2]==33 && words[i+3]<255) binding[words[i+1]]=(uint8_t)words[i+3];
            if(words[i+2]==34 && words[i+3]<255) set[words[i+1]]=(uint8_t)words[i+3];
        }
        i+=n;
    }
    uint32_t buffer=UINT32_MAX;
    for(uint32_t id=0;id<bound;id++) if(binding[id]==0 && set[id]==0) {buffer=id;break;}
    if(buffer==UINT32_MAX) goto done;
    int last=-1,pre=-1,first=-1,rows[4]={0},components[4]={0};uint32_t points[4]={0};unsigned candidates=0;
    for(size_t i=5;i<count;) {
        uint32_t n=words[i]>>16,op=words[i]&65535;
        if(op==65 && n==6 && words[i+3]==buffer && words[i+5]<bound && known[words[i+5]]) {
            int row=(int)constants[words[i+5]];
            size_t p=i+n;
            if(p+4<count && (words[p]&65535)==61 && words[p]>>16==4 && words[p+3]==words[i+2]) {
                uint32_t loaded=words[p+2];p+=4;
                if(p+4<count && (words[p]&65535)==61 && words[p]>>16==4) {
                    uint32_t position=words[p+2],point=words[p+3];p+=4;
                    if(p+9<count && (words[p]&65535)==79 && words[p]>>16==9 && words[p+3]==position && words[p+4]==position &&
                       words[p+5]==words[p+6] && words[p+5]==words[p+7] && words[p+5]==words[p+8]) {
                        uint32_t splat=words[p+2],component=words[p+5];p+=9;
                        if(p+5<=count && (words[p]&65535)==133 && words[p]>>16==5 &&
                           ((words[p+3]==loaded && words[p+4]==splat)||(words[p+4]==loaded && words[p+3]==splat))) {
                            if(candidates==0) {pre=last;first=(int)i;}
                            unsigned k=candidates%4;rows[k]=row;components[k]=(int)component;points[k]=point;candidates++;
                            if(k==3) {
                                int valid=rows[0]==rows[3]+3 && rows[1]==rows[3]+2 && rows[2]==rows[3]+1 &&
                                    components[0]==3 && components[1]==2 && components[2]==1 && components[3]==0 &&
                                    points[0]==points[1] && points[0]==points[2] && points[0]==points[3];
                                if(!valid) {candidates=0;first=-1;}
                                else if(!result.matrix_a) result.matrix_a=rows[3];
                                else {
                                    result.matrix_b=rows[3];result.translation=pre;result.local_origin=result.matrix_a-2;
                                    result.valid=first>=0 && pre>result.matrix_b+3 && result.matrix_a>1 && result.matrix_b==result.matrix_a+4 && pre<1024;
                                    goto done;
                                }
                            }
                        }
                    }
                }
            }
            last=row;
        }
        i+=n;
    }
done:
    free(constants);free(known);free(binding);free(set);return result;
}
#endif
