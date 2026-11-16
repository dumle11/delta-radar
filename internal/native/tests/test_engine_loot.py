"""Production catalog, sparse maps, UTF-16 names, price/rarity and filter checks."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
source = (root / "internal/native/engine_visibility.h").read_text()
helpers = source[source.index("typedef struct { uint64_t data;"):source.index("/* Bind the UGPPhysics")]
helpers = helpers.replace("static int ev_read(uint64_t address, void *out, size_t size) {",
                          "static int ev_read(uint64_t address, void *out, size_t size) { ++fixture_reads;")
scanner = (root / "external/scanner/memscan-legacy.c").read_text()
fstring = scanner[scanner.index("static int read_fstring("):scanner.index("\n}",scanner.index("static int read_fstring("))+2]
program = r'''
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "self-memory.h"
#define OFF_GUOBJECTARRAY_FROM_BASE 0x206aef48
#define OFF_UOBJECT_NAME 0x1c
#define MAX_ACTORS 8
static uint64_t ue4_base;
static unsigned fixture_reads;
''' + helpers + r'''
static int is_any_ptr(uint64_t p) { return p>65536 && p<0xffffffffffffULL && !(p&3); }
static uint64_t read_ptr(uint64_t p) { return ev_ptr(p); }
static int32_t read_i32(uint64_t p) { int32_t n=0;ev_read(p,&n,4);return n; }
static ssize_t read_mem(uint64_t p,void *out,size_t n) { return ev_read(p,out,n)?(ssize_t)n:-1; }
''' + fstring + r'''
typedef struct { EVObject object; int32_t serial; } EFRef;
static int ef_ref(uint64_t p,EFRef *ref) { uint64_t item;
    return ev_object(p,&ref->object) && ev_item(ref->object.index,&item) && ev_read(item+0x14,&ref->serial,4); }
static int ef_same_function(const EFRef *ref) { EFRef now={0};
    return ef_ref(ref->object.address,&now) && now.serial==ref->serial && now.object.name==ref->object.name; }
static void resolve_fname(int32_t id,char *out,size_t n) {
    snprintf(out,n,"%s",id==10?"DataTable":id==11?"DFMCommonItemRow":id==12?"DFMColorDefineRow":id==30?"A004":id==20?"1000042":id==21?"1000043":id==22?"1000044":"Other"); }
static void _diag(const char *message) { (void)message; }
static uint64_t fixture_container;
static int ew_is_a(uint64_t object,const char *wanted,uint32_t size) {(void)size;return object==fixture_container && !strcmp(wanted,"InventoryPickup_Container");}
enum {LOOT_NONE,LOOT_ITEM,LOOT_CONTAINER,LOOT_STASH,LOOT_DEATH_BOX};
typedef struct { char kind[16],loot_name[256],id[40];float x,y,z,half_height; uint64_t actor_address,loot_unit_value;
    int loot_kind,loot_quality,loot_metadata_known,loot_value_known,loot_color_known,loot_stack,loot_line,loot_in_container; uint32_t loot_color; } Entity;
static Entity entities[MAX_ACTORS];
#include "engine_loot.h"
#include "loot_filter.h"
static unsigned char objects[7][0x3000], items[7][24], array[0x28], row[3][0x100], row_struct[2][0x80], text_data[0x60];
static uint64_t chunks[1];
static struct {uint64_t key,row;uint64_t hash;} pairs[3],colors[1];
static void put32(void *p,int32_t n) { memcpy(p,&n,4); }
static void put64(void *p,uint64_t n) { memcpy(p,&n,8); }
static void putf(void *p,float n) { memcpy(p,&n,4); }
static void map(int object,void *data,int count,unsigned flags) {
    put64(objects[object]+0x30,(uintptr_t)data);put32(objects[object]+0x38,count);put32(objects[object]+0x3c,count);
    put32(objects[object]+0x40,flags);put32(objects[object]+0x58,count);put32(objects[object]+0x5c,128);
}
int main(void) {
    ue4_base=(uintptr_t)array-OFF_GUOBJECTARRAY_FROM_BASE;chunks[0]=(uintptr_t)items;
    put32(array+0x14,7);put64(array+0x20,(uintptr_t)chunks);
    for(int i=0;i<7;i++) {
        put64(objects[i]+8,(uintptr_t)objects[0]);put32(objects[i]+0x1c,100+i);put32(objects[i]+0x24,i);
        put64(items[i],(uintptr_t)objects[i]);put32(items[i]+0x14,1000+i);
    }
    el_table_class=(uintptr_t)objects[0];put32(row_struct[0]+0x1c,11);put32(row_struct[1]+0x1c,12);
    put64(objects[1]+0x28,(uintptr_t)row_struct[0]);put64(objects[2]+0x28,(uintptr_t)row_struct[1]);
    pairs[0].key=20;pairs[0].row=(uintptr_t)row[0];pairs[1].key=21;pairs[1].row=(uintptr_t)row[1];
    pairs[2].key=22;pairs[2].row=(uintptr_t)row[2];
    map(1,pairs,3,5);map(2,colors,1,1);
    for(int i=0;i<3;i++) {put64(row[i]+0x10,20+i);put32(row[i]+0x68,4);put32(row[i]+0xdc,12000);put64(row[i]+0x18,(uintptr_t)text_data);}
    uint16_t unicode[]={0x0041,0x004b,0x004d,0x0020,0x00c9,0x03b1,0};
    put64(text_data+0x48,(uintptr_t)unicode);put32(text_data+0x50,7);put32(text_data+0x54,7);
    colors[0].key=30;colors[0].row=(uintptr_t)row[1];
    putf(row[1]+0x10,0.5);putf(row[1]+0x14,0.0);putf(row[1]+0x18,1.0);putf(row[1]+0x1c,1.0);
    el_consider_table((uintptr_t)objects[1]);el_consider_table((uintptr_t)objects[2]);el_load_rows();
    assert(el_table_count==2 && el_row_count==2 && !el_find(21,0)); // Deleted sparse slot ignored.
    char name[256];int q=0,known=0;uint64_t value=0;
    assert(el_metadata(20,name,sizeof(name),&q,&value,&known));
    assert(!strcmp(name,"AKM \xc3\x89\xce\xb1") && q==4 && known && value==12000);
    assert(el_numeric_key(1000042)==20 && !el_numeric_key(1000043));
    unsigned char contents[2][0x900]={0};put32(contents[0]+0x10,100);put32(contents[0]+0x14,42);put32(contents[0]+0x38,3);
    fixture_container=(uintptr_t)objects[5];put64(objects[5]+0x2290,(uintptr_t)contents);put32(objects[5]+0x2298,2);put32(objects[5]+0x229c,2);
    Entity parent={.actor_address=fixture_container,.x=1,.y=2,.z=3};
    assert(el_append_contents(&parent,0)==1 && entities[0].loot_stack==3 && entities[0].loot_in_container && entities[0].loot_line==1);
    assert(entities[0].loot_quality==4 && entities[0].loot_unit_value==12000 && entities[0].x==1);
    assert(!strcmp(entities[0].loot_name,"AKM \xc3\x89\xce\xb1"));
    assert(el_color_known[4] && el_colors[4]==0xffbc00ff); // Linear RGB -> sRGB.
    // Actual UI QualityColorMap uses int32/FLinearColor + hash links, stride 28.
    struct {int32_t quality;float rgba[4];int32_t hash[2];} widget_colors[2]={
        {2,{.25f,.5f,.75f,1},{0,0}},{5,{1,0,0,1},{0,0}}};
    assert(sizeof(widget_colors[0])==28);
    uint64_t widget_map=(uintptr_t)objects[4]+0x348;
    put64((void*)widget_map,(uintptr_t)widget_colors);put32((void*)(widget_map+8),2);put32((void*)(widget_map+12),2);
    put32((void*)(widget_map+0x10),1);put32((void*)(widget_map+0x28),2);put32((void*)(widget_map+0x2c),128);
    assert(ef_ref((uintptr_t)objects[4],&el_palette_widgets[0].object));el_palette_widgets[0].offset=0x348;el_palette_widget_count=1;
    el_tick_count=60;el_load_widget_palettes();
    assert(el_color_known[2] && el_colors[2]==0xff89bce1 && !el_color_known[5]);
    assert(loot_matches(q,1,value,1,0,1u<<4,12000,1,100,100)); // Inclusive value/range boundaries.
    assert(!loot_matches(q,1,value,1,0,1u<<3,0,1,10,100));
    assert(!loot_matches(q,1,value,1,0,255,12001,1,10,100)); // Per-item, never multiplied by stack.
    assert(!loot_matches(q,1,value,1,1,255,0,0,10,100));
    assert(!loot_matches(q,1,value,1,0,255,0,1,100.1,100));
    assert(!loot_matches(q,1,value,1,0,255,0,1,NAN,100));
    put32(row[0]+0xdc,-1);assert(el_metadata(20,name,sizeof(name),&q,&value,&known) && !known && q==4);
    assert(!loot_matches(q,1,value,known,0,255,1,1,10,100)); // Unknown value cannot pass positive filter.
    put32(objects[1]+0x40,4);assert(!el_metadata(20,name,sizeof(name),&q,&value,&known)); // Removed entry.
    put32(objects[1]+0x40,5);pairs[0].row=(uintptr_t)row[2];assert(!el_metadata(20,name,sizeof(name),&q,&value,&known));
    pairs[0].row=(uintptr_t)row[0];put32(items[1]+0x14,9999);assert(!el_metadata(20,name,sizeof(name),&q,&value,&known));
    assert(loot_matches(99,0,0,0,0,1,0,1,10,100));
    assert(!loot_matches(99,0,0,0,0,0xfe,0,1,10,100));
    // A missing localization must never retry the entire catalog each frame.
    put32(items[1]+0x14,1001);put32(text_data+0x50,0);
    assert(el_metadata(20,name,sizeof(name),&q,&value,&known) && q==4 && !strcmp(name,"Unknown item"));
    ELRow existing=el_rows[el_hash(20)];
    for(unsigned i=0;i<EL_ROW_SLOTS;i++) el_rows[i]=existing;
    el_row_count=EL_ROW_SLOTS;el_tick_count=201;
    unsigned before=fixture_reads;el_update(0);el_update(0);
    assert(fixture_reads-before<2000);
    puts("PASS: game table classes, sparse holes, UTF-16 names, rarity/unit value, native colors, unknown values, filters, distance, removed/recycled metadata");
    self_memory_close(&ev_reader);
}
'''
with tempfile.TemporaryDirectory() as directory:
    src, binary = Path(directory) / "loot.c", Path(directory) / "loot"
    src.write_text(program)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-unused-variable",
                    "-I", str(root / "internal/native"), "-I", str(root / "external/scanner"), str(src), "-lm", "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
