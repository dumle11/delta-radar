import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.os.Looper;
import android.view.View;
import android.view.MotionEvent;
import android.view.WindowManager;
import java.lang.reflect.Proxy;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import android.widget.Switch;
import java.io.FileOutputStream;

/** Runs the real overlay with synthetic frames through Android app_process. */
public class OverlayRegression {
    static void check(boolean ok, String message) {
        if (!ok) throw new AssertionError(message);
    }

    static void drag(View handle, float fromX, float fromY, float toX, float toY) {
        long now = android.os.SystemClock.uptimeMillis();
        int[] actions = {MotionEvent.ACTION_DOWN, MotionEvent.ACTION_MOVE, MotionEvent.ACTION_UP};
        for (int i = 0; i < actions.length; i++) {
            MotionEvent event = MotionEvent.obtain(now, now + i * 20, actions[i],
                i == 0 ? fromX : toX, i == 0 ? fromY : toY, 0);
            check(handle.dispatchTouchEvent(event), "header consumes drag gesture");
            event.recycle();
        }
    }

    public static void main(String[] args) throws Exception {
        if (Looper.myLooper() == null) Looper.prepare();
        Class<?> threadClass = Class.forName("android.app.ActivityThread");
        Object thread = threadClass.getMethod("systemMain").invoke(null);
        Context context = (Context) threadClass.getMethod("getSystemContext").invoke(thread);
        O view = new O(context);
        O.ps = new Paint(); O.pf = new Paint(); O.pt = new Paint(); O.bp = new Paint();
        O.rdy = true;
        view.onSizeChanged(320, 180, 0, 0);
        check(O.b(), "begin measured frame");
        check(O.gw() == 320 && O.gh() == 180, "measured viewport");
        O.f(10, 10, 30, 30, 0xFFFF0000);
        O.e();
        Bitmap result = Bitmap.createBitmap(320, 180, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(result);
        view.onDraw(canvas);
        check(result.getPixel(20, 20) == 0xFFFF0000, "published frame visible");

        Canvas[] cached = new Canvas[O.frames.length];
        for (int i = 0; i < cached.length; i++) cached[i] = O.frames[i].canvas;
        check(O.b(), "begin incomplete frame");
        O.f(10, 10, 30, 30, 0xFF00FF00);
        canvas.drawColor(0, PorterDuff.Mode.CLEAR);
        long before = android.os.SystemClock.uptimeMillis();
        view.onDraw(canvas);
        check(android.os.SystemClock.uptimeMillis() - before < 100, "UI never waits for producer");
        check(result.getPixel(20, 20) == 0xFFFF0000, "incomplete frame preserves completed pixels");
        O.e();
        canvas.drawColor(0, PorterDuff.Mode.CLEAR); view.onDraw(canvas);
        check(result.getPixel(20, 20) == 0xFF00FF00, "completion publishes new pixels");
        O.Frame pinned = O.publishedFrame;
        pinned.readers.incrementAndGet();
        for (int i = 0; i < 4; i++) {
            check(O.b(), "spare frame available while UI reads old frame");
            check(O.drawingFrame != pinned, "pinned frame is never overwritten");
            O.f(10, 10, 30, 30, 0xFFFF0000); O.e();
        }
        pinned.readers.decrementAndGet();
        for (O.Frame frame : O.frames) if (frame != O.publishedFrame) frame.readers.incrementAndGet();
        check(!O.b(), "busy spare frames drop production");
        canvas.drawColor(0, PorterDuff.Mode.CLEAR); view.onDraw(canvas);
        check(result.getPixel(20, 20) == 0xFFFF0000, "busy production cannot blank UI");
        for (O.Frame frame : O.frames) if (frame != O.publishedFrame) frame.readers.decrementAndGet();
        for (int i = 0; i < 2000; i++) {
            check(O.b(), "begin reusable frame");
            O.f(10, 10, 30, 30, 0xFFFF0000); O.e();
            boolean reused = false;
            for (Canvas entry : cached) reused |= O.frontCanvas == entry;
            check(reused, "canvas reused");
        }

        check(O.b(), "begin FOV circle frame");
        O.ps.setStyle(Paint.Style.STROKE);
        O.cr(160, 90, 30, 0xFF00FF00); O.e();
        O.espEnabled = false; O.aimEnabled = true;
        canvas.drawColor(0, PorterDuff.Mode.CLEAR); view.onDraw(canvas);
        check(result.getPixel(190, 90) == 0xFF00FF00, "FOV circle centered and visible with ESP disabled");
        check(result.getPixel(160, 90) == 0, "FOV circle leaves crosshair clear");
        O.aimEnabled = false;
        O.espEnabled = false;
        canvas.drawColor(0, PorterDuff.Mode.CLEAR);
        view.onDraw(canvas);
        check(result.getPixel(20, 20) == 0, "disabled overlay invisible");
        check(O.b(), "can clear while disabled");
        O.e(); O.espEnabled = true;
        view.onDraw(canvas);
        check(result.getPixel(20, 20) == 0, "empty frame clears previous pixels");

        check(O.b(), "begin frame before resize");
        O.f(10, 10, 30, 30, 0xFFFF0000);
        view.onSizeChanged(180, 320, 320, 180);
        O.e();
        check(O.frameAt == 0, "resize rejects frame from previous viewport");
        check(O.b(), "resize buffers on producer");
        check(O.gw() == 180 && O.gh() == 320, "portrait dimensions preserved");
        O.f(10, 10, 30, 30, 0xFFFF0000); O.e();
        Thread.sleep(O.FRAME_TTL_MS + 30);
        canvas.drawColor(0, PorterDuff.Mode.CLEAR);
        view.onDraw(canvas);
        check(result.getPixel(20, 20) == 0, "stopped producer expires");
        O.MenuPanel panel = new O.MenuPanel(context);
        final int[] moves = {0};
        WindowManager originalWm = O.wm;
        O.wm = (WindowManager) Proxy.newProxyInstance(WindowManager.class.getClassLoader(),
            new Class<?>[] {WindowManager.class}, new InvocationHandler() {
                public Object invoke(Object proxy, Method method, Object[] arguments) {
                    if (method.getName().equals("updateViewLayout")) moves[0]++;
                    return null;
                }
            });
        O.viewWidth = 1920; O.viewHeight = 1080;
        WindowManager.LayoutParams position = new WindowManager.LayoutParams();
        position.width = 760; position.height = 700; position.x = 100; position.y = 100;
        panel.setLayoutParams(position);
        drag(panel.dragHandle, 10, 10, 210, 110);
        check(position.x == 300 && position.y == 200 && moves[0] > 0, "header drags menu window");
        drag(panel.dragHandle, 10, 10, 5010, 5010);
        check(position.x == 1160 && position.y == 380, "drag clamps to bottom/right");
        drag(panel.dragHandle, 10, 10, -5010, -5010);
        check(position.x == 0 && position.y == 0 && O.menuX == 0 && O.menuY == 0,
            "drag clamps to top/left and remembers position");
        O.wm = originalWm;
        Switch skeleton = null;
        for (int i = 0; i < panel.body.getChildCount(); i++) {
            View child = panel.body.getChildAt(i);
            if (child instanceof Switch && ((Switch) child).getText().toString().equals("Skeleton"))
                skeleton = (Switch) child;
        }
        check(skeleton != null, "per-entity skeleton control exists");
        skeleton.setChecked(true);
        check((O.gf(panel.selectedEntityType) & O.F_SKELETON) != 0, "skeleton toggle is wired");
        skeleton.setChecked(false);
        panel.selectTab(2);
        Switch autoShoot = null;
        for (int i = 0; i < panel.body.getChildCount(); i++) {
            View child = panel.body.getChildAt(i);
            if (child instanceof Switch && ((Switch) child).getText().toString().equals("Auto shoot"))
                autoShoot = (Switch) child;
        }
        check(autoShoot != null, "Auto shoot switch restored in Aim tab");
        O.aimEnabled = false;
        autoShoot.setChecked(true);
        check(O.gaa() && !O.ga(), "Trigger works independently of Aim");
        O.menuShown = true; check(!O.gaa(), "Menu pauses trigger requests");
        O.menuShown = false;
        autoShoot.setChecked(false); check(!O.gaa(), "Auto shoot switch disables trigger");
        panel.selectTab(1); check(panel.selectedTab == 1, "color tab opens");
        check(O.ET_NAMES[O.ET_NPC].equals("NPCs") && O.ET_NAMES[O.ET_BOT_PLAYER].equals("Bot Players"),
            "NPCs and simulated players have distinct menu entries");
        int oldColor = O.gc(1); O.setColor(1, 0xFF123456);
        check(O.gc(1) == 0xFF123456, "native color getter follows selection"); O.setColor(1, oldColor);
        for (int type = 0; type < O.ET_COUNT; type++) {
            check(O.gvc(type, true) == 0xFF00FF00, "visible defaults green");
            check(O.gvc(type, false) == 0xFFFF0000, "occluded defaults red");
        }
        O.setVisibilityColor(1, true, 0xFF123456);
        O.setVisibilityColor(1, false, 0xFF654321);
        check(O.gvc(1, true) == 0xFF123456 && O.gvc(1, false) == 0xFF654321,
            "visible and occluded selections are independent");
        check(O.gvc(0, true) == 0xFF00FF00 && O.gvc(0, false) == 0xFFFF0000,
            "entity palettes are independent");
        java.io.File settingsFile = java.io.File.createTempFile("overlay-colors-", ".xml",
            new java.io.File("/data/local/tmp"));
        settingsFile.delete();
        java.lang.reflect.Constructor<?> prefsConstructor = Class.forName("android.app.SharedPreferencesImpl")
            .getDeclaredConstructor(java.io.File.class, int.class);
        prefsConstructor.setAccessible(true);
        O.prefs = (android.content.SharedPreferences) prefsConstructor.newInstance(settingsFile, 0);
        O.setVisibilityColor(O.ET_NPC, true, 0xFF112233);
        O.setVisibilityColor(O.ET_BOT_PLAYER, true, 0xFF445566);
        check(O.grd() == 0, "ray debug defaults off");
        panel.selectTab(4);
        Switch rayToggle = null;
        for (int i = 0; i < panel.body.getChildCount(); i++) {
            View child = panel.body.getChildAt(i);
            if (child instanceof Switch && ((Switch)child).getText().toString().equals("Enable raycast debug"))
                rayToggle = (Switch)child;
        }
        check(rayToggle != null, "ray debug control exists in Status");
        rayToggle.setChecked(true); check(O.grd() == 15, "ray debug toggle reaches native flags");
        O.rayViews = false; check(O.grd() == 13, "actor rays toggle independent of LOS and bounds");
        O.saveSettings();
        check(O.prefs.edit().commit(), "flush saved palette to disk");
        android.content.SharedPreferences reloaded = (android.content.SharedPreferences)
            prefsConstructor.newInstance(settingsFile, 0);
        check(reloaded.getInt("visibleColor1", 0) == 0xFF123456 &&
            reloaded.getInt("occludedColor1", 0) == 0xFF654321 &&
            reloaded.getInt("visibleColor0", 0) == 0xFF00FF00,
            "entity and visibility palettes persist to disk independently");
        check(reloaded.getInt("visibleColor.npc", 0) == 0xFF112233 &&
            reloaded.getInt("visibleColor.botPlayer", 0) == 0xFF445566,
            "NPC and player-bot palettes persist independently of old bot/team slots");
        check(reloaded.getBoolean("rayDebug", false) && !reloaded.getBoolean("rayViews", true) &&
            reloaded.getBoolean("rayLos", false) && reloaded.getBoolean("rayBounds", false),
            "ray debug controls persist independently");
        O.rayDebug = false; O.rayViews = true;
        O.prefs = null; settingsFile.delete();
        panel.colorSlot = 1; panel.colorVisible = false; panel.selectTab(1);
        android.widget.TextView swatch = new android.widget.TextView(context);
        panel.updateSwatch(swatch);
        check(swatch.getText().toString().contains("Occluded") &&
            swatch.getText().toString().contains("654321"), "swatch follows entity and visibility");
        panel.selectTab(4); check(panel.status != null, "live status tab opens");
        for (int tab = 0; tab < 5; tab++) {
            panel.selectTab(tab);
            panel.measure(View.MeasureSpec.makeMeasureSpec(760, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(700, View.MeasureSpec.EXACTLY));
            panel.layout(0, 0, 760, 700);
            Bitmap preview = Bitmap.createBitmap(760, 700, Bitmap.Config.ARGB_8888);
            panel.draw(new Canvas(preview));
            FileOutputStream stream = new FileOutputStream("/data/local/tmp/delta-menu-" + tab + ".png");
            preview.compress(Bitmap.CompressFormat.PNG, 100, stream); stream.close(); preview.recycle();
        }
        System.out.println("PASS: viewport, pinned frames, incomplete-frame retention, canvas reuse, expiry, menu dragging, tabs, skeleton toggle, palette");
        System.exit(0);
    }
}
