import android.app.Activity;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.PorterDuff;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;

public class O extends View {
    static volatile Bitmap front, back;
    static Paint ps, pf, pt, bp;
    static int sw, sh;
    static volatile boolean rdy;
    static Canvas cv;
    static O inst;
    static WindowManager wm;
    static Handler uiHandler;
    static volatile boolean initDone;

    static BubbleView bubble;
    static MenuPanel menu;

    static volatile boolean espEnabled = true;
    static volatile boolean healthBars = true;
    static volatile boolean distLabels = true;
    static volatile boolean snaplines = false;

    static volatile String diagGWorld = "---";
    static volatile String diagEntities = "---";
    static volatile String diagFName = "---";
    static volatile String diagStatus = "---";
    static volatile String diagExtra = "";

    O(Context c) { super(c); }

    static void z(final Activity a) {
        if (initDone) return;
        initDone = true;
        uiHandler = new Handler(Looper.getMainLooper());
        a.runOnUiThread(new Runnable() {
            public void run() {
                try {
                    wm = (WindowManager) a.getSystemService(Context.WINDOW_SERVICE);
                    sw = a.getResources().getDisplayMetrics().widthPixels;
                    sh = a.getResources().getDisplayMetrics().heightPixels;
                    if (sw < sh) { int t = sw; sw = sh; sh = t; }

                    inst = new O(a);
                    inst.setWillNotDraw(false);
                    WindowManager.LayoutParams espLp = new WindowManager.LayoutParams(
                        WindowManager.LayoutParams.MATCH_PARENT,
                        WindowManager.LayoutParams.MATCH_PARENT,
                        WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                        WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                        WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE |
                        WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                        PixelFormat.TRANSLUCENT);
                    espLp.gravity = Gravity.TOP | Gravity.LEFT;
                    wm.addView(inst, espLp);

                    front = Bitmap.createBitmap(sw, sh, Bitmap.Config.ARGB_8888);
                    back = Bitmap.createBitmap(sw, sh, Bitmap.Config.ARGB_8888);
                    ps = new Paint(); ps.setStyle(Paint.Style.STROKE); ps.setStrokeWidth(3f); ps.setAntiAlias(true);
                    pf = new Paint(); pf.setStyle(Paint.Style.FILL); pf.setAntiAlias(false);
                    pt = new Paint(); pt.setStyle(Paint.Style.FILL); pt.setAntiAlias(true); pt.setTextSize(24f);
                    bp = new Paint(); bp.setFilterBitmap(false);

                    bubble = new BubbleView(a);
                    WindowManager.LayoutParams blp = new WindowManager.LayoutParams(
                        90, 90,
                        WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                        WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                        WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                        PixelFormat.TRANSLUCENT);
                    blp.gravity = Gravity.TOP | Gravity.LEFT;
                    blp.x = 10;
                    blp.y = sh / 2 - 45;
                    wm.addView(bubble, blp);

                    menu = new MenuPanel(a);
                    rdy = true;
                } catch (Exception ex) {
                    initDone = false;
                }
            }
        });
    }

    @Override protected void onDraw(Canvas c) {
        Bitmap f = front;
        if (f != null && !f.isRecycled()) c.drawBitmap(f, 0, 0, bp);
    }

    /* ---- ESP draw API (called from scanner thread) ---- */
    static boolean b() {
        if (!rdy || !espEnabled) return false;
        Bitmap bk = back;
        if (bk == null || bk.isRecycled()) return false;
        try { cv = new Canvas(bk); cv.drawColor(0, PorterDuff.Mode.CLEAR); }
        catch (Exception e) { cv = null; return false; }
        return true;
    }
    static void r(float x1, float y1, float x2, float y2, int c) {
        if (cv == null) return; ps.setColor(c); cv.drawRect(x1, y1, x2, y2, ps);
    }
    static void f(float x1, float y1, float x2, float y2, int c) {
        if (cv == null) return; pf.setColor(c); cv.drawRect(x1, y1, x2, y2, pf);
    }
    static void l(float x1, float y1, float x2, float y2, int c, float w) {
        if (cv == null) return; ps.setColor(c);
        float ow = ps.getStrokeWidth(); ps.setStrokeWidth(w);
        cv.drawLine(x1, y1, x2, y2, ps); ps.setStrokeWidth(ow);
    }
    static void t(String txt, float x, float y, int c) {
        if (cv == null) return; pt.setColor(c); cv.drawText(txt, x, y, pt);
    }
    static void e() {
        cv = null;
        Bitmap tmp = front; front = back; back = tmp;
        if (inst != null) inst.postInvalidate();
    }

    static int gw() { return sw; }
    static int gh() { return sh; }
    static boolean ok() { return rdy; }

    /* ---- Diagnostic updates ---- */
    static void dg(String gw) { diagGWorld = gw; refreshMenu(); }
    static void de(String ent) { diagEntities = ent; refreshMenu(); }
    static void df(String fn) { diagFName = fn; refreshMenu(); }
    static void ds(String st) { diagStatus = st; refreshMenu(); }
    static void dx(String ex) { diagExtra = ex; refreshMenu(); }

    /* ---- Toggle queries ---- */
    static boolean ge() { return espEnabled; }
    static boolean gh2() { return healthBars; }
    static boolean gd() { return distLabels; }
    static boolean gs() { return snaplines; }

    static volatile boolean menuShown = false;

    static void toggleMenu() {
        if (wm == null || menu == null || bubble == null) return;
        if (menuShown) {
            try { wm.removeView(menu); } catch (Exception ex) {}
            menuShown = false;
        } else {
            WindowManager.LayoutParams mlp = new WindowManager.LayoutParams(
                480, 520,
                WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                PixelFormat.TRANSLUCENT);
            WindowManager.LayoutParams blp = (WindowManager.LayoutParams) bubble.getLayoutParams();
            mlp.gravity = Gravity.TOP | Gravity.LEFT;
            mlp.x = blp.x + 100;
            mlp.y = blp.y - 100;
            if (mlp.y < 0) mlp.y = 10;
            wm.addView(menu, mlp);
            menuShown = true;
        }
        bubble.invalidate();
    }

    static void refreshMenu() {
        if (menuShown && menu != null) {
            if (uiHandler != null) uiHandler.post(new Runnable() {
                public void run() { if (menu != null && menuShown) menu.invalidate(); }
            });
        }
    }

    /* ======== Floating bubble ======== */
    static class BubbleView extends View {
        Paint cp, tp;
        float downRawX, downRawY;
        int origX, origY;
        boolean dragging;
        long downTime;

        BubbleView(Context c) {
            super(c);
            cp = new Paint(Paint.ANTI_ALIAS_FLAG);
            tp = new Paint(Paint.ANTI_ALIAS_FLAG);
            tp.setColor(0xFFFFFFFF);
            tp.setTextSize(32f);
            tp.setTypeface(Typeface.DEFAULT_BOLD);
            tp.setTextAlign(Paint.Align.CENTER);
        }

        @Override protected void onDraw(Canvas c) {
            float cx = getWidth() / 2f, cy = getHeight() / 2f, r = Math.min(cx, cy) - 2;
            cp.setColor(menuShown ? 0xCCFF6D00 : 0xCC00C853);
            c.drawCircle(cx, cy, r, cp);
            c.drawText("R", cx, cy + 11, tp);
        }

        @Override public boolean onTouchEvent(MotionEvent ev) {
            switch (ev.getAction()) {
                case MotionEvent.ACTION_DOWN:
                    downRawX = ev.getRawX(); downRawY = ev.getRawY();
                    WindowManager.LayoutParams lp = (WindowManager.LayoutParams) getLayoutParams();
                    origX = lp.x; origY = lp.y;
                    dragging = false; downTime = System.currentTimeMillis();
                    return true;
                case MotionEvent.ACTION_MOVE:
                    float mx = ev.getRawX() - downRawX, my = ev.getRawY() - downRawY;
                    if (Math.abs(mx) > 10 || Math.abs(my) > 10) dragging = true;
                    if (dragging && wm != null) {
                        WindowManager.LayoutParams lp2 = (WindowManager.LayoutParams) getLayoutParams();
                        lp2.x = origX + (int) mx;
                        lp2.y = origY + (int) my;
                        wm.updateViewLayout(this, lp2);
                    }
                    return true;
                case MotionEvent.ACTION_UP:
                    if (!dragging && System.currentTimeMillis() - downTime < 300) {
                        toggleMenu();
                    }
                    return true;
            }
            return super.onTouchEvent(ev);
        }
    }

    /* ======== Menu panel ======== */
    static class MenuPanel extends View {
        Paint bgP, txtP, hdrP, togOnP, togOffP, lineP;
        String[] toggleNames = {"ESP Boxes", "Health Bars", "Distance", "Snap Lines"};

        MenuPanel(Context c) {
            super(c);
            bgP = new Paint(); bgP.setColor(0xDD1A1A2E);
            txtP = new Paint(Paint.ANTI_ALIAS_FLAG); txtP.setColor(0xFFE0E0E0); txtP.setTextSize(26f);
            hdrP = new Paint(Paint.ANTI_ALIAS_FLAG); hdrP.setColor(0xFF00E676); hdrP.setTextSize(28f);
            hdrP.setTypeface(Typeface.DEFAULT_BOLD);
            togOnP = new Paint(Paint.ANTI_ALIAS_FLAG); togOnP.setColor(0xFF00C853);
            togOffP = new Paint(Paint.ANTI_ALIAS_FLAG); togOffP.setColor(0xFF555555);
            lineP = new Paint(); lineP.setColor(0x33FFFFFF);
        }

        @Override protected void onDraw(Canvas c) {
            float w = getWidth(), h = getHeight();
            c.drawRoundRect(new RectF(0, 0, w, h), 16, 16, bgP);

            boolean[] states = {espEnabled, healthBars, distLabels, snaplines};
            float y = 36;
            c.drawText("DR", 16, y, hdrP);
            y += 12;
            c.drawLine(8, y, w - 8, y, lineP);

            for (int i = 0; i < toggleNames.length; i++) {
                y += 52;
                c.drawText(toggleNames[i], 16, y, txtP);
                float tx = w - 70, ty = y - 22;
                c.drawRoundRect(new RectF(tx, ty, tx + 52, ty + 28), 14, 14,
                    states[i] ? togOnP : togOffP);
                Paint kp = new Paint(Paint.ANTI_ALIAS_FLAG); kp.setColor(0xFFFFFFFF);
                c.drawCircle(states[i] ? tx + 30 : tx + 22, ty + 14, 10, kp);
            }

            y += 28;
            c.drawLine(8, y, w - 8, y, lineP);

            Paint dp = new Paint(Paint.ANTI_ALIAS_FLAG);
            dp.setColor(0xFFAABBCC); dp.setTextSize(20f); dp.setTypeface(Typeface.MONOSPACE);
            y += 28; c.drawText("GW: " + diagGWorld, 12, y, dp);
            y += 26; c.drawText("Ent: " + diagEntities, 12, y, dp);
            y += 26; c.drawText("FN: " + diagFName, 12, y, dp);
            y += 26; c.drawText("ST: " + diagStatus, 12, y, dp);
            if (diagExtra != null && diagExtra.length() > 0) {
                y += 26; dp.setTextSize(16f); c.drawText(diagExtra, 12, y, dp);
            }
        }

        @Override public boolean onTouchEvent(MotionEvent ev) {
            if (ev.getAction() == MotionEvent.ACTION_UP) {
                float y = ev.getY();
                float startY = 48;
                for (int i = 0; i < 4; i++) {
                    float ry = startY + i * 52;
                    if (y >= ry && y < ry + 52) {
                        switch (i) {
                            case 0: espEnabled = !espEnabled; break;
                            case 1: healthBars = !healthBars; break;
                            case 2: distLabels = !distLabels; break;
                            case 3: snaplines = !snaplines; break;
                        }
                        invalidate();
                        return true;
                    }
                }
            }
            return true;
        }
    }
}
