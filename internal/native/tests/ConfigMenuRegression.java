import android.content.Context;
import android.os.Looper;
import android.view.View;
import android.view.ViewGroup;
import android.view.MotionEvent;
import android.widget.SeekBar;
import android.widget.Switch;
import java.io.File;
import org.json.JSONObject;
public class ConfigMenuRegression {
 static void check(boolean ok,String message){if(!ok)throw new AssertionError(message);}
 static View find(View root,String label){if(label.equals(root.getContentDescription())||label.equals(root.getTag()))return root;if(root instanceof android.widget.TextView&&label.equals(((android.widget.TextView)root).getText().toString()))return root;if(root instanceof ViewGroup)for(int i=0;i<((ViewGroup)root).getChildCount();i++){View v=find(((ViewGroup)root).getChildAt(i),label);if(v!=null)return v;}return null;}
 public static void main(String[]args)throws Exception{
  Looper.prepare();Class<?> type=Class.forName("android.app.ActivityThread");Object thread=type.getMethod("systemMain").invoke(null);Context context=(Context)type.getMethod("getSystemContext").invoke(thread);
  File root=new File("/data/local/tmp/config-fixture-"+System.nanoTime());root.mkdirs();O.configRoot=root;
  File prefs=new File(root,"prefs.xml");java.lang.reflect.Constructor<?> constructor=Class.forName("android.app.SharedPreferencesImpl").getDeclaredConstructor(File.class,int.class);constructor.setAccessible(true);O.prefs=(android.content.SharedPreferences)constructor.newInstance(prefs,0);
  for(int i=0;i<O.ET_COUNT;i++){O.entityFeatures[i]=O.defaultFeatures(i);O.entityRenderDist[i]=200;}
  O.menuX=123;O.menuY=80;O.aimFov=24;O.aim360=false;O.noWeaponSway=true;
  File saved=O.ConfigFiles.write(null,"Firing range",O.settingsSnapshot());check(saved.getName().equals("config-Firing range.json"),"File has a readable profile name");
  JSONObject config=O.ConfigFiles.read(saved);check(config.getJSONObject("settings").getInt("aimFov")==24,"Settings serialized");check(!config.getJSONObject("settings").has("menuX"),"UI position excluded from gameplay profile");
  O.aimFov=8;O.noWeaponSway=false;O.applyConfig(config.getJSONObject("settings"));check(O.aimFov==24&&O.noWeaponSway&&O.menuX==123,"Load restores settings without moving UI");
  File renamed=O.ConfigFiles.write(saved,"Ranked",config.getJSONObject("settings"));check(!saved.exists()&&renamed.exists()&&O.ConfigFiles.list().length==1,"Rename changes file/name without duplication");
  boolean rejected=false;try{O.ConfigFiles.write(null,"Ranked",O.settingsSnapshot());}catch(java.io.IOException expected){rejected=true;}check(rejected,"Duplicate names do not overwrite a profile");
  rejected=false;try{O.ConfigFiles.write(new File(root.getParentFile(),"escape.json"),"Escape",O.settingsSnapshot());}catch(java.io.IOException expected){rejected=true;}check(rejected,"Files outside config directory are rejected");
  JSONObject invalid=new JSONObject();invalid.put("aimFov",11);invalid.put("aim","bad boolean");rejected=false;try{O.applyConfig(invalid);}catch(java.io.IOException expected){rejected=true;}check(rejected&&O.aimFov==24,"Invalid file applies no partial settings");
  JSONObject edit=O.ConfigFiles.read(renamed);edit.getJSONObject("settings").put("aimFov",32);renamed=O.ConfigFiles.write(renamed,"Ranked",edit.getJSONObject("settings"));O.applyConfig(O.ConfigFiles.read(renamed).getJSONObject("settings"));check(O.aimFov==32,"Editing and reloading a config works");
  O.MenuPanel panel=new O.MenuPanel(context);ViewGroup choices=(ViewGroup)find(panel,"visual-actor-types");check(choices!=null&&choices.getChildCount()==3,"Visuals has only enemy/teammate/NPC controls");
  panel.selectTab(2);panel.measure(View.MeasureSpec.makeMeasureSpec(1520,View.MeasureSpec.EXACTLY),View.MeasureSpec.makeMeasureSpec(940,View.MeasureSpec.EXACTLY));panel.layout(0,0,1520,940);
  SeekBar fov=(SeekBar)find(panel,"Aim FOV radius");check(fov!=null&&fov.isEnabled(),"FOV radius is available without 360");int[] xy=new int[2];fov.getLocationOnScreen(xy);check(xy[1]<400,"FOV control is near the top");
  Switch mode=(Switch)find(panel,"360° targeting");mode.setChecked(true);check(!find(panel,"Aim FOV radius").isEnabled(),"360 mode explicitly disables radius");((Switch)find(panel,"360° targeting")).setChecked(false);check(find(panel,"Aim FOV radius").isEnabled(),"Disabling 360 immediately restores radius");
  panel.selectTab(7);check(find(panel,"New from current")!=null&&find(panel,"Load")!=null,"Config actions are reachable");find(panel,"New from current").performClick();check(find(panel,"q")!=null,"In-game name keyboard is available");
  find(panel,"q").performClick();find(panel,"w").performClick();check(panel.nameDraft.equals("qw"),"Keyboard edits draft without Android overlay/IME");
  O.ConfigFiles.delete(renamed);check(O.ConfigFiles.list().length==0,"Delete removes the selected file");O.prefs.edit().commit();O.prefs=null;for(File file:root.listFiles())file.delete();root.delete();
  System.out.println("PASS: named config files, atomic validation, save/load/edit/rename/delete, path isolation, player-only selectors and reachable FOV mode controls");System.exit(0);
 }
}
