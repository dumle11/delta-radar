import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.os.Looper;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Switch;
import android.widget.SeekBar;
import android.view.MotionEvent;
import java.io.FileOutputStream;
public class MenuLayoutRegression {
 static void check(boolean ok,String msg){if(!ok)throw new AssertionError(msg);}
 static Switch find(View v,String name){if(v instanceof Switch&&name.equals(((Switch)v).getText().toString()))return (Switch)v;if(v instanceof ViewGroup)for(int i=0;i<((ViewGroup)v).getChildCount();i++){Switch s=find(((ViewGroup)v).getChildAt(i),name);if(s!=null)return s;}return null;}
 static SeekBar slider(View v,int max){if(v instanceof SeekBar&&((SeekBar)v).getMax()==max)return (SeekBar)v;if(v instanceof ViewGroup)for(int i=0;i<((ViewGroup)v).getChildCount();i++){SeekBar b=slider(((ViewGroup)v).getChildAt(i),max);if(b!=null)return b;}return null;}
 public static void main(String[] args)throws Exception{
  Looper.prepare();Class<?> t=Class.forName("android.app.ActivityThread");Object instance=t.getMethod("systemMain").invoke(null);Context context=(Context)t.getMethod("getSystemContext").invoke(instance);
  for(int i=0;i<O.ET_COUNT;i++){O.entityFeatures[i]=127;O.entityRenderDist[i]=200;}O.chamsEnabled=true;
  O.configRoot=new java.io.File("/data/local/tmp/menu-config-fixture-"+System.nanoTime());O.configRoot.mkdirs();
  O.MenuPanel panel=new O.MenuPanel(context);
  for(int tab=0;tab<8;tab++){
   panel.selectTab(tab);for(int i=0;i<2;i++){panel.scrolls[i].animate().cancel();panel.scrolls[i].setAlpha(1);}
   panel.measure(View.MeasureSpec.makeMeasureSpec(1520,View.MeasureSpec.EXACTLY),View.MeasureSpec.makeMeasureSpec(940,View.MeasureSpec.EXACTLY));panel.layout(0,0,1520,940);
   check(panel.nav.getChildCount()==8,"All navigation destinations remain available");
   check(panel.columns[0].getWidth()>300&&panel.columns[1].getWidth()>300,"Both columns have readable widths");
   if(tab==0){SeekBar distance=slider(panel,200);check(distance!=null,"Render distance slider exists");long now=android.os.SystemClock.uptimeMillis();
    for(int action:new int[]{MotionEvent.ACTION_DOWN,MotionEvent.ACTION_MOVE,MotionEvent.ACTION_UP}){MotionEvent event=MotionEvent.obtain(now,now+20,action,distance.getWidth()*(action==MotionEvent.ACTION_DOWN?.2f:.8f),distance.getHeight()/2f,0);distance.dispatchTouchEvent(event);event.recycle();}
    check(slider(panel,200)==distance,"Dragging keeps the same slider and page alive");check(O.entityRenderDist[0]>200,"Slider updates the selected actor's distance");O.entityRenderDist[0]=200;
   }
   if(tab==3){Switch sway=find(panel,"No Weapon Sway");check(sway!=null,"Sway setting present");boolean old=O.noWeaponSway;sway.setChecked(!old);check(O.noWeaponSway!=old,"Sway switch retains binding");sway.setChecked(old);check(find(panel,"Force third person")!=null,"Perspective setting retained");}
   if(tab==6)check(find(panel,"Enable chams")!=null,"Chams has a dedicated page");
   Bitmap bitmap=Bitmap.createBitmap(1520,940,Bitmap.Config.ARGB_8888);panel.draw(new Canvas(bitmap));FileOutputStream out=new FileOutputStream("/data/local/tmp/delta-menu-page-"+tab+".png");bitmap.compress(Bitmap.CompressFormat.PNG,100,out);out.close();bitmap.recycle();
  }
  System.out.println("PASS: eight Android pages, readable columns, continuous slider gesture, retained sway/perspective bindings and dedicated Chams page");System.exit(0);
 }
}
