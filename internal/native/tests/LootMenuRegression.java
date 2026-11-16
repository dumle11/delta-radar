import android.content.Context;
import android.os.Looper;
import android.view.View;
import android.widget.Switch;
import android.widget.TextView;
import java.io.File;

/** Runs actual Loot controls and persisted settings without interacting with the game. */
public class LootMenuRegression {
    static void check(boolean value,String why) { if (!value) throw new AssertionError(why); }
    static Switch find(O.MenuPanel panel,String label) {
        for(int i=0;i<panel.body.getChildCount();i++) {
            View child=panel.body.getChildAt(i);
            if(child instanceof Switch && ((Switch)child).getText().toString().equals(label)) return (Switch)child;
        }
        throw new AssertionError("Missing switch: "+label);
    }
    public static void main(String[] args) throws Exception {
        if(Looper.myLooper()==null)Looper.prepare();
        Class<?> type=Class.forName("android.app.ActivityThread");
        Object thread=type.getMethod("systemMain").invoke(null);
        Context context=(Context)type.getMethod("getSystemContext").invoke(thread);
        File file=File.createTempFile("loot-prefs-",".xml",new File("/data/local/tmp"));file.delete();
        java.lang.reflect.Constructor<?> constructor=Class.forName("android.app.SharedPreferencesImpl")
            .getDeclaredConstructor(File.class,int.class);constructor.setAccessible(true);
        O.prefs=(android.content.SharedPreferences)constructor.newInstance(file,0);
        for(int i=0;i<O.ET_COUNT;i++) {O.entityFeatures[i]=O.defaultFeatures(i);O.entityRenderDist[i]=200;}
        O.viewWidth=1920;O.viewHeight=1080;
        O.MenuPanel panel=new O.MenuPanel(context);
        panel.selectedEntityType=O.ET_ENEMY;panel.selectTab(5);
        check(panel.selectedTab==5 && panel.nav.getChildCount()==6,"dedicated Loot sub-menu");
        int enemy=O.gf(O.ET_ENEMY);
        Switch enabled=find(panel,"Show loot");enabled.setChecked(false);
        check((O.gf(O.ET_LOOT)&O.F_ENABLED)==0 && O.gf(O.ET_ENEMY)==enemy,"Loot toggle targets Loot after menu restores selection");
        enabled.setChecked(true);
        find(panel,"Item names & stack counts").setChecked(false);
        check((O.gf(O.ET_LOOT)&O.F_NAMES)==0,"name display toggle wired");
        find(panel,"Distance labels").setChecked(true);
        check((O.gf(O.ET_LOOT)&O.F_DISTANCE)!=0,"distance display toggle wired");
        find(panel,"Gold").setChecked(false);check((O.glr()&(1<<5))==0,"rarity selection reaches native mask");
        find(panel,"Unknown / unclassified").setChecked(false);check((O.glr()&1)==0,"unknown items independently selectable");
        find(panel,"Show reference value per item").setChecked(false);check((O.glf()&1)==0,"value display independent");
        find(panel,"Include containers, stashes & death boxes").setChecked(false);check((O.glf()&2)==0,"container selection independent");
        O.lootMinValue=25000;O.entityRenderDist[O.ET_LOOT]=350;
        O.saveSettings();check(O.prefs.edit().commit(),"persist settings");
        android.content.SharedPreferences saved=(android.content.SharedPreferences)constructor.newInstance(file,0);
        check(saved.getInt("lootMinValue",-1)==25000 && saved.getInt("lootRarityMask",0)==O.glr(),"value and rarity persist");
        check(saved.getInt(O.entityKey("rdist",O.ET_LOOT),0)==350 && O.gr(O.ET_ENEMY)==200,"independent Loot range persists");
        check(!saved.getBoolean("lootShowValue",true) && !saved.getBoolean("lootShowContainers",true),"display filters persist");
        O.lc(5,0xffffcc33);panel.selectTab(5);
        check(find(panel,"Gold").getCurrentTextColor()==0xffffcc33,"game-provided color used without replacing character palettes");
        panel.selectedEntityType=O.ET_LOOT;panel.selectTab(0);
        boolean link=false;
        for(int i=0;i<panel.body.getChildCount();i++) {
            View child=panel.body.getChildAt(i);
            if(child instanceof TextView && ((TextView)child).getText().toString().equals("Open loot settings")) {child.performClick();link=true;break;}
        }
        check(link && panel.selectedTab==5,"Visuals Loot entry links to sub-menu");
        check(!O.noRecoil && !O.noSpread && !O.infiniteStamina,"loot controls preserve unrelated settings");
        O.prefs=null;file.delete();
        System.out.println("PASS: Loot menu, rarity/unit-value/range preferences, game palette, display filters, links and independent entity settings");
        System.exit(0);
    }
}
