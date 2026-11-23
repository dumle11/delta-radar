import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.os.Looper;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;

/** Actual Android layout, offscreen drawing and touch routing, no game changes. */
public class RenderHostRegression {
    static void check(boolean condition,String message) { if(!condition) throw new AssertionError(message); }
    public static void main(String[] args) throws Exception {
        Looper.prepare();
        Class<?> thread=Class.forName("android.app.ActivityThread");
        Object instance=thread.getMethod("systemMain").invoke(null);
        Context context=(Context)thread.getMethod("getSystemContext").invoke(instance);
        O.RenderHost host=new O.RenderHost(context);
        O.wm=new O.LocalWindows(host);
        O.inst=new O(context);
        O.ps=new Paint();O.pf=new Paint();O.pt=new Paint();O.bp=new Paint();
        O.rdy=true;
        WindowManager.LayoutParams full=new WindowManager.LayoutParams();full.width=-1;full.height=-1;
        O.wm.addView(O.inst,full);
        O.bubble=new O.BubbleView(context);
        WindowManager.LayoutParams button=new WindowManager.LayoutParams();button.width=90;button.height=90;button.x=200;button.y=80;
        button.gravity=Gravity.LEFT|Gravity.TOP;
        O.wm.addView(O.bubble,button);
        host.measure(View.MeasureSpec.makeMeasureSpec(400,View.MeasureSpec.EXACTLY),View.MeasureSpec.makeMeasureSpec(240,View.MeasureSpec.EXACTLY));
        host.layout(0,0,400,240);
        check(O.gw()==400 && O.gh()==240,"viewport follows game activity content");
        check(O.b(),"begin ESP frame");O.f(10,10,30,30,Color.RED);O.e();
        Bitmap screen=Bitmap.createBitmap(400,240,Bitmap.Config.ARGB_8888);
        Canvas canvas=new Canvas(screen);
        host.draw(canvas);
        check(screen.getPixel(20,20)==0 && screen.getPixel(245,125)==0,"Android window draws no UI pixels");
        host.paintChildren(canvas);
        check(screen.getPixel(20,20)==Color.RED,"ESP pixels reach offscreen image");
        check(Color.alpha(screen.getPixel(230,110))>0,"floating button reaches same image");
        long now=android.os.SystemClock.uptimeMillis();
        MotionEvent outside=MotionEvent.obtain(now,now,MotionEvent.ACTION_DOWN,100,210,0);
        check(!host.dispatchTouchEvent(outside),"outside controls passes through to game");outside.recycle();
        MotionEvent down=MotionEvent.obtain(now,now,MotionEvent.ACTION_DOWN,245,125,0);
        check(host.dispatchTouchEvent(down),"button handles touch in game activity");down.recycle();
        // Cancel avoids invoking the menu during this isolated fixture.
        MotionEvent cancel=MotionEvent.obtain(now,now+20,MotionEvent.ACTION_CANCEL,245,125,0);
        host.dispatchTouchEvent(cancel);cancel.recycle();
        O.wm.updateViewLayout(O.bubble,button);
        check(button.x==200 && button.y==80,"position retained by local layout adapter");
        System.out.println("RenderHost: offscreen ESP/button, transparent window and touch passthrough passed");
        System.exit(0);
    }
}
