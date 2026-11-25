import android.content.Context;
import android.os.Looper;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Switch;
import java.io.File;

/** Tests the real Misc switch and persisted preference without injecting game input. */
public class StaminaMenuRegression {
    static void check(boolean value, String why) { if (!value) throw new AssertionError(why); }
    static Switch find(View view,String label) {
        if(view instanceof Switch && label.equals(((Switch)view).getText().toString()))return (Switch)view;
        if(view instanceof ViewGroup)for(int i=0;i<((ViewGroup)view).getChildCount();i++){Switch found=find(((ViewGroup)view).getChildAt(i),label);if(found!=null)return found;}
        return null;
    }
    public static void main(String[] args) throws Exception {
        if (Looper.myLooper() == null) Looper.prepare();
        Class<?> type = Class.forName("android.app.ActivityThread");
        Object thread = type.getMethod("systemMain").invoke(null);
        Context context = (Context)type.getMethod("getSystemContext").invoke(thread);
        File file = File.createTempFile("stamina-prefs-", ".xml", new File("/data/local/tmp"));
        file.delete();
        java.lang.reflect.Constructor<?> constructor = Class.forName("android.app.SharedPreferencesImpl")
            .getDeclaredConstructor(File.class, int.class);
        constructor.setAccessible(true);
        O.prefs = (android.content.SharedPreferences)constructor.newInstance(file, 0);
        O.viewWidth = 1920; O.viewHeight = 1080;
        O.MenuPanel panel = new O.MenuPanel(context);
        panel.selectTab(3);
        Switch stamina=find(panel,"Infinite stamina");
        check(stamina != null && !stamina.isChecked() && !O.gis(), "Misc switch defaults off");
        stamina.setChecked(true);
        check(O.gis(), "enabled reaches native getter");
        check(O.prefs.edit().commit(), "flush enabled preference");
        android.content.SharedPreferences saved = (android.content.SharedPreferences)constructor.newInstance(file, 0);
        check(saved.getBoolean("infiniteStamina", false), "enabled setting persists");
        stamina.setChecked(false);
        check(!O.gis() && !O.prefs.getBoolean("infiniteStamina", true), "disable reaches getter and preferences");
        check(!O.noRecoil && !O.noSpread, "movement switch leaves weapon toggles unchanged");
        O.prefs.edit().commit(); O.prefs = null; file.delete();
        System.out.println("PASS: Misc stamina toggle, default off, native getter, enable/disable persistence, independent weapon controls");
        System.exit(0);
    }
}
