#ifndef INTERNAL_ENTITY_PRESENTATION_H
#define INTERNAL_ENTITY_PRESENTATION_H
/* Stable renderer category ABI: enemy0, teammate1, NPC2, retired bot3, loot4. */
static int actor_display_type(char kind,int player_bot,int team) {
    if(kind=='l')return 4;
    if(kind=='a' && !player_bot)return 2;
    return team==1?1:0;
}
static const char *actor_kind_label(char kind,int player_bot) {
    if(kind=='l')return "LOOT";
    if(player_bot)return "BOT";
    return kind=='p'?"PLR":"NPC";
}
#endif
