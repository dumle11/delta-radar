#ifndef DELTA_ENTITY_LIFE_H
#define DELTA_ENTITY_LIFE_H
/* GPGameplay.ECharacterLiveStatus: Alive=1, Death=2, ImpendingDeath=3.
 * A downed player's Health can be zero before its final death/box state. */
static int entity_observed_dead(int health_known,float health,float alive,
                               int character_dead,int player_dead,
                               int life_known,int life,int death_box) {
    int health_alive=health_known && health>0.5f;
    if(life_known) {
        if(death_box) return 1;
        if(life==3 || (life==1 && !character_dead && !player_dead)) return 0;
        if(life==2 && !health_alive) return 1;
    }
    return (health_known && alive<0.5f && health<=0.01f) ||
        ((character_dead || player_dead) && !health_alive);
}
#endif
