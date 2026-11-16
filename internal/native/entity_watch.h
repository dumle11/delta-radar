#ifndef INTERNAL_ENTITY_WATCH_H
#define INTERNAL_ENTITY_WATCH_H
/* Bounded diagnostics for a target that disappears after taking damage. */
static void entity_watch_update(int count) {
    static EFRef watched; static int64_t next_log; static unsigned previous=~0u;
    uint64_t target=atomic_load(&ea_locked_actor);
    if(target && target!=watched.object.address) {ef_ref(target,&watched);previous=~0u;}
    if(!watched.object.address || ef_now()<next_log) return;
    next_log=ef_now()+1000;
    int present=0,dead=0;float hp=0,max=0,alive=0;
    for(int i=0;i<count;i++) if(entities[i].actor_address==watched.object.address) {
        present=1;dead=entities[i].dead;hp=entities[i].health;break;
    }
    int identity=ef_same(&watched);
    uint64_t state=identity?ev_ptr(watched.object.address+OFF_APAWN_PLAYER_STATE):0;
    int active=identity && actor_is_active_participant(watched.object.address,state);
    int life=0,box=0;read_player_life_status(state,&life,&box);
    unsigned status=(unsigned)(identity | present<<1 | dead<<2 | active<<3 | life<<4 | box<<8);
    if(status==previous) return;
    previous=status;
    if(identity) read_actor_health(watched.object.address,&hp,&max,&alive);
    uint8_t flags=0,finished=0,ps_dead=0;
    if(state) {ev_read(state+OFF_PLAYER_STATE_BOT_FLAGS,&flags,1);ev_read(state+OFF_PLAYER_STATE_FINISHED,&finished,1);ev_read(state+OFF_PLAYER_STATE_DEAD,&ps_dead,1);}
    char text[250];snprintf(text,sizeof(text),"target presence: actor=%lx identity=%d present=%d dead=%d active=%d hp=%.1f alive=%.1f flags=%02x finished=%u ps_dead=%u life=%d box=%d",
        (unsigned long)watched.object.address,identity,present,dead,active,hp,alive,flags,finished,ps_dead,life,box);_diag(text);
    if(!identity) memset(&watched,0,sizeof(watched));
}
#endif
