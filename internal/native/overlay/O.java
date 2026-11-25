import android.app.Activity;
import android.app.Application;
import android.os.Bundle;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.LayerDrawable;
import android.graphics.drawable.ClipDrawable;
import android.util.TypedValue;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Switch;
import android.widget.SeekBar;
import android.widget.CompoundButton;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.PorterDuff;
import android.graphics.RectF;
import android.graphics.Path;
import android.graphics.drawable.RippleDrawable;
import android.content.res.ColorStateList;
import android.view.animation.DecelerateInterpolator;
import android.graphics.Typeface;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.view.WindowManager;
import java.util.concurrent.atomic.AtomicInteger;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.Map;
import org.json.JSONObject;

public class O extends View {
    static volatile Bitmap front, back;
    static Paint ps, pf, pt, bp;
    static Paint actorText, actorOutline;
    static volatile int sw, sh;
    static volatile int viewWidth, viewHeight;
    static final class Frame {
        final Bitmap bitmap;
        final Canvas canvas;
        final int width, height;
        final AtomicInteger readers = new AtomicInteger();
        volatile long presentedAt;
        Frame(int w, int h) {
            width = w; height = h;
            bitmap = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            canvas = new Canvas(bitmap);
        }
    }
    static Frame[] frames;
    static volatile Frame publishedFrame;
    static Frame drawingFrame;
    static int menuX = -1, menuY = -1;
    static volatile long frameAt;
    static final long FRAME_TTL_MS = 10000;
    static volatile boolean rdy;
    static Canvas cv, frontCanvas, backCanvas;
    static O inst;
    static LocalWindows wm;
    static RenderHost renderHost;
    static long uiRevision;
    static final Runnable attachHost = new Runnable() {
        public void run() {
            try {
                // Startup can finish the splash before our UI Runnable executes.
                // Resolve the current game activity instead of retaining its decor.
                Class<?> thread=Class.forName("android.app.ActivityThread");
                Object current=thread.getMethod("currentActivityThread").invoke(null);
                java.lang.reflect.Field records=thread.getDeclaredField("mActivities");
                records.setAccessible(true);
                java.util.Map<?,?> activities=(java.util.Map<?,?>)records.get(current);
                for(Object record:activities.values()) {
                    java.lang.reflect.Field field=record.getClass().getDeclaredField("activity");
                    field.setAccessible(true);
                    Activity activity=(Activity)field.get(record);
                    if(activity==null || activity.isFinishing() || !activity.getClass().getName().endsWith("GameActivity")) continue;
                    ViewGroup decor=(ViewGroup)activity.getWindow().getDecorView();
                    if(renderHost!=null && renderHost.getParent()!=decor) {
                        if(renderHost.getParent() instanceof ViewGroup) ((ViewGroup)renderHost.getParent()).removeView(renderHost);
                        decor.addView(renderHost,new ViewGroup.LayoutParams(-1,-1));
                        renderEvent("attached current game activity");
                    }
                    break;
                }
            } catch(Exception ex) { renderEvent("attach failed " + ex.toString()); }
            if(uiHandler!=null) uiHandler.postDelayed(this,1000);
        }
    };
    static native void submitPixels(Bitmap bitmap,int rotation);
    static native boolean compositorActive();
    static native void renderEvent(String message);
    static native void inputRegions(float[] rectangles);
    static boolean routeTouch(final int action,final float x,final float y,final long down,final long time) {
        if(uiHandler==null || renderHost==null) return false;
        return uiHandler.post(new Runnable() {
            public void run() {
                MotionEvent event=MotionEvent.obtain(down,time,action,x,y,0);
                renderHost.dispatchTouchEvent(event);event.recycle();
            }
        });
    }

    // Child views retain their existing layout and input behavior. They never
    // draw into Android's window surface: pixels go to the game Vulkan image.
    static final class RenderHost extends ViewGroup {
        Bitmap pixels;
        Canvas canvas;
        long lastSubmitGeneration, lastUiRevision = -1;
        final Runnable capture = new Runnable() {
            public void run() {
                if (!isAttachedToWindow()) return;
                int w=getWidth(), h=getHeight();
                if (w>0 && h>0) {
                    if (pixels==null || pixels.getWidth()!=w || pixels.getHeight()!=h) {
                        if(pixels!=null) pixels.recycle();
                        pixels=Bitmap.createBitmap(w,h,Bitmap.Config.ARGB_8888);
                        canvas=new Canvas(pixels);
                        lastSubmitGeneration = -1;
                    }
                    Frame pf = publishedFrame;
                    long gen = pf != null ? pf.presentedAt : 0;
                    if (gen != lastSubmitGeneration || menuShown || lastUiRevision != uiRevision) {
                        canvas.drawColor(Color.TRANSPARENT,PorterDuff.Mode.CLEAR);
                        paintChildren(canvas);
                        submitPixels(pixels,getDisplay()==null?0:getDisplay().getRotation());
                        lastSubmitGeneration = gen; lastUiRevision = uiRevision;
                    }
                    int count=0;
                    for(int i=0;i<getChildCount();i++) if(getChildAt(i)!=inst && getChildAt(i).getVisibility()==View.VISIBLE) count++;
                    float[] hits=new float[Math.min(3,count)*4];int n=0;
                    for(int i=0;i<getChildCount() && n<hits.length;i++) {
                        View child=getChildAt(i);
                        if(child==inst || child.getVisibility()!=View.VISIBLE) continue;
                        hits[n++]=child.getLeft();hits[n++]=child.getTop();hits[n++]=child.getRight();hits[n++]=child.getBottom();
                    }
                    inputRegions(hits);
                }
                postDelayed(this, menuShown ? 16 : 33);
            }
        };
        RenderHost(Context context) { super(context); setWillNotDraw(true); setClipChildren(false); }
        @Override protected void onAttachedToWindow() { super.onAttachedToWindow(); renderEvent("host attached"); post(capture); }
        @Override protected void onDetachedFromWindow() { removeCallbacks(capture); super.onDetachedFromWindow(); }
        @Override protected void dispatchDraw(Canvas canvas) { /* Vulkan presents the pixels. */ }
        void paintChildren(Canvas canvas) { super.dispatchDraw(canvas); }
        @Override protected void onMeasure(int ws,int hs) {
            int w=MeasureSpec.getSize(ws), h=MeasureSpec.getSize(hs);
            setMeasuredDimension(w,h);
            for(int i=0;i<getChildCount();i++) {
                View child=getChildAt(i);
                WindowManager.LayoutParams lp=(WindowManager.LayoutParams)child.getLayoutParams();
                int cw=lp.width<0?w:Math.min(w,lp.width), ch=lp.height<0?h:Math.min(h,lp.height);
                child.measure(MeasureSpec.makeMeasureSpec(cw,MeasureSpec.EXACTLY),MeasureSpec.makeMeasureSpec(ch,MeasureSpec.EXACTLY));
            }
        }
        @Override protected void onLayout(boolean changed,int l,int t,int r,int b) {
            for(int i=0;i<getChildCount();i++) {
                View child=getChildAt(i);
                WindowManager.LayoutParams lp=(WindowManager.LayoutParams)child.getLayoutParams();
                int w=child.getMeasuredWidth(),h=child.getMeasuredHeight();
                int x=(lp.gravity&Gravity.HORIZONTAL_GRAVITY_MASK)==Gravity.RIGHT?getWidth()-w-lp.x:lp.x;
                int y=lp.y;
                child.layout(x,y,x+w,y+h);
            }
        }
    }
    // This adapter stores layout data only; it does not call WindowManager.
    static final class LocalWindows {
        final RenderHost host;
        LocalWindows(RenderHost host) { this.host=host; }
        void addView(View view,WindowManager.LayoutParams lp) { host.addView(view,lp); uiRevision++; }
        void removeView(View view) { host.removeView(view); uiRevision++; }
        void updateViewLayout(View view,WindowManager.LayoutParams lp) {
            lp.x=clampPosition(lp.x,host.getWidth(),view.getWidth());
            lp.y=clampPosition(lp.y,host.getHeight(),view.getHeight());
            view.setLayoutParams(lp); host.requestLayout(); uiRevision++;
        }
    }
    static Handler uiHandler;
    static volatile boolean initDone;

    static BubbleView bubble;
    static MenuPanel menu;
    static LockBtnView lockBtn;

    static SharedPreferences prefs;
    static volatile float lineWidth = 2.5f;
    static final int[] defaultColors = {0xFFE5534B, 0xFF46D9FF, 0xFF4FD08A, 0xFF4BCA81, 0xFF4BCA81};
    static volatile int[] entityColors = {defaultColors[0], defaultColors[1], defaultColors[2], defaultColors[3], defaultColors[4]};
    static final int VISIBLE_COLOR = 0xFF00FF00, OCCLUDED_COLOR = 0xFFFF0000;
    static volatile int[] visibleColors = new int[5], occludedColors = new int[5];
    static {
        java.util.Arrays.fill(visibleColors, VISIBLE_COLOR);
        java.util.Arrays.fill(occludedColors, OCCLUDED_COLOR);
    }
    static volatile boolean espEnabled = true;
    static volatile boolean rayDebug = false, rayViews = true, rayLos = true, rayBounds = true;

    static final int ET_ENEMY = 0, ET_TEAMMATE = 1, ET_NPC = 2, ET_BOT_PLAYER = 3, ET_LOOT = 4;
    static final int ET_COUNT = 5;
    static final String[] ET_NAMES = {"Enemies", "Teammates", "NPCs", "Bot Players", "Loot"};

    static final int F_ENABLED = 1, F_BOXES = 2, F_SKELETON = 4, F_NAMES = 8,
                      F_HEALTH = 16, F_DISTANCE = 32, F_SNAPLINES = 64;
    static volatile int[] entityFeatures = new int[ET_COUNT];
    static volatile int[] entityRenderDist = new int[ET_COUNT];
    static volatile int snapOrigin = 0; // 0=bottom, 1=top

    // Aim settings
    static volatile boolean aimEnabled = false;
    static volatile int aimFov = 15;
    static volatile int aimStrength = 100;
    static volatile int aimBone = 2; // 0=head, 1=chest, 2=body
    static volatile boolean autoShoot = false;
    static volatile boolean autoZoom = false;
    static volatile boolean showLockBtn = false;
    static volatile boolean aimLocked = true;
    static volatile boolean aim360 = false;
    static volatile int aimTargetMask = 0x5; // enemies + bots
    static volatile int aimPriority = 0; // 0=crosshair, 1=distance, 2=low hp, 3=looking at us
    static volatile int aimMaxDist = 0; // 0=unlimited, else meters
    static volatile int aimTypePref = 0; // 0=none, 1=enemy, 2=teammate, 3=npc

    // Misc settings
    static volatile boolean noRecoil = false;
    static volatile boolean noSpread = false;
    static volatile boolean noWeaponSway = false;
    static volatile boolean infiniteStamina = false;
    static volatile int cameraFov = 0; // 0 = game default, 10-170 = override
    static volatile boolean forceTPP = false;

    // Chams settings
    static volatile boolean chamsEnabled = false;
    static volatile int chamsBrightness = 70;
    static volatile int chamsVisibleColor = 0xFF00FF00;
    static volatile int chamsInvisibleColor = 0xFFFF0000;

    // Loot values are the game's reference unit prices, not live market quotes.
    static volatile int lootRarityMask = 255;
    static volatile int lootMinValue = 0;
    static volatile boolean lootShowValue = true, lootShowContainers = true;
    static volatile int[] lootGameColors = {0xffdedede,0xffdedede,0xffdedede,0xffdedede,0xffdedede,0xffdedede,0xffdedede,0xffdedede};
    static final String[] LOOT_RARITIES = {"Unknown / unclassified", "White", "Green", "Blue", "Purple", "Gold", "Red", "Special"};

    static final int AIM_ENEMIES = 1, AIM_TEAMMATES = 2, AIM_BOTS = 4;
    static final int AIM_ALL = AIM_ENEMIES | AIM_TEAMMATES | AIM_BOTS;

    static volatile String diagGWorld = "---";
    static volatile String diagEntities = "---";
    static volatile String diagFName = "---";
    static volatile String diagStatus = "---";
    static volatile String diagExtra = "";
    static volatile String diagEntList = "";

    O(Context c) { super(c); }

    static synchronized void z(final Activity a) {
        if (initDone) return;
        initDone = true;
        uiHandler = new Handler(Looper.getMainLooper());
        a.runOnUiThread(new Runnable() {
            public void run() {
                try {
                    prefs = a.getSharedPreferences("overlay_v2", Context.MODE_PRIVATE);
                    configRoot=new File(a.getFilesDir(),"configs");
                    loadSettings();

                    renderHost = new RenderHost(a);
                    ViewGroup decor=(ViewGroup)a.getWindow().getDecorView();
                    decor.addView(renderHost,new ViewGroup.LayoutParams(-1,-1));
                    wm = new LocalWindows(renderHost);
                    a.getApplication().registerActivityLifecycleCallbacks(new Application.ActivityLifecycleCallbacks() {
                        public void onActivityResumed(Activity current) {
                            if (!current.getClass().getName().endsWith("GameActivity") || renderHost==null) return;
                            renderEvent("resume " + current.getClass().getName());
                            ViewGroup next=(ViewGroup)current.getWindow().getDecorView();
                            if(renderHost.getParent()!=next) {
                                if(renderHost.getParent() instanceof ViewGroup) ((ViewGroup)renderHost.getParent()).removeView(renderHost);
                                next.addView(renderHost,new ViewGroup.LayoutParams(-1,-1));
                            }
                        }
                        public void onActivityCreated(Activity current,Bundle state) {}
                        public void onActivityStarted(Activity current) {}
                        public void onActivityPaused(Activity current) {}
                        public void onActivityStopped(Activity current) {}
                        public void onActivitySaveInstanceState(Activity current,Bundle state) {}
                        public void onActivityDestroyed(Activity current) {}
                    });
                    sh = a.getResources().getDisplayMetrics().heightPixels;

                    inst = new O(a);
                    inst.setWillNotDraw(false);
                    inst.setLayerType(View.LAYER_TYPE_SOFTWARE, null);
                    WindowManager.LayoutParams espLp = new WindowManager.LayoutParams(
                        WindowManager.LayoutParams.MATCH_PARENT,
                        WindowManager.LayoutParams.MATCH_PARENT,
                        WindowManager.LayoutParams.TYPE_APPLICATION,
                        WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                        WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE |
                        WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                        PixelFormat.TRANSLUCENT);
                    espLp.gravity = Gravity.TOP | Gravity.LEFT;
                    wm.addView(inst, espLp);

                    ps = new Paint(); ps.setStyle(Paint.Style.STROKE); ps.setStrokeWidth(3f); ps.setAntiAlias(true);
                    pf = new Paint(); pf.setStyle(Paint.Style.FILL); pf.setAntiAlias(false);
                    pt = new Paint(); pt.setStyle(Paint.Style.FILL); pt.setAntiAlias(true); pt.setTextSize(24f);
                    bp = new Paint(); bp.setFilterBitmap(false);

                    bubble = new BubbleView(a);
                    WindowManager.LayoutParams blp = new WindowManager.LayoutParams(
                        90, 90,
                        WindowManager.LayoutParams.TYPE_APPLICATION,
                        WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                        WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                        PixelFormat.TRANSLUCENT);
                    blp.gravity = Gravity.TOP | Gravity.LEFT;
                    blp.x = 10;
                    blp.y = sh / 2 - 45;
                    wm.addView(bubble, blp);

                    lockBtn = new LockBtnView(a);
                    WindowManager.LayoutParams lklp = new WindowManager.LayoutParams(
                        70, 70,
                        WindowManager.LayoutParams.TYPE_APPLICATION,
                        WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                        WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                        PixelFormat.TRANSLUCENT);
                    lklp.gravity = Gravity.TOP | Gravity.RIGHT;
                    lklp.x = 20;
                    lklp.y = 100;
                    wm.addView(lockBtn, lklp);
                    lockBtn.setVisibility((showLockBtn && aimEnabled) ? View.VISIBLE : View.GONE);

                    menu = new MenuPanel(a);
                    rdy = true;
                    uiHandler.post(new Runnable() {
                        public void run() {
                            if (inst == null) return;
                            if (frameAt != 0 && SystemClock.uptimeMillis() - frameAt > FRAME_TTL_MS)
                                inst.invalidate();
                            uiHandler.postDelayed(this, 2000);
                        }
                    });
                    uiHandler.post(attachHost);
                    renderEvent("UI initialized " + a.getClass().getName());
                } catch (Exception ex) {
                    renderEvent("init failed " + ex.toString());
                    initDone = false;
                }
            }
        });
    }

    static int defaultFeatures(int type) {
        if (type == ET_LOOT) return F_ENABLED | F_BOXES | F_NAMES;
        if (type == ET_TEAMMATE) return F_ENABLED | F_BOXES | F_NAMES | F_HEALTH;
        return F_ENABLED | F_BOXES | F_NAMES | F_HEALTH | F_DISTANCE | F_SNAPLINES;
    }

    static String entityKey(String setting, int type) {
        if (type == ET_NPC) return setting + ".npc";
        if (type == ET_BOT_PLAYER) return setting + ".botPlayer";
        return setting + type;
    }

    @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        viewWidth = w; viewHeight = h; frameAt = 0;
    }

    @Override protected void onDraw(Canvas c) {
        if (!espEnabled && !aimEnabled) return;
        if (frameAt==0 || SystemClock.uptimeMillis()-frameAt>FRAME_TTL_MS) return;
        Frame f = publishedFrame;
        if (f == null || f.bitmap == null) return;
        f.readers.incrementAndGet();
        try {
            c.drawBitmap(f.bitmap, 0, 0, bp);
        } finally { f.readers.decrementAndGet(); }
    }

    static boolean b() {
        if (!rdy || cv != null) return false;
        int w = viewWidth, h = viewHeight;
        if (w <= 0 || h <= 0) return false;
        if (frames == null || frames[0].width != w || frames[0].height != h) {
            Frame[] resized = {new Frame(w, h), new Frame(w, h), new Frame(w, h)};
            frames = resized; sw = w; sh = h;
        }
        Frame target = null;
        for (Frame candidate : frames) {
            if (candidate != publishedFrame && candidate.readers.get() == 0) {
                target = candidate; break;
            }
        }
        if (target == null) return false;
        drawingFrame = target; back = target.bitmap; backCanvas = target.canvas;
        try {
            cv = target.canvas; cv.drawColor(0, PorterDuff.Mode.CLEAR);
            ps.setStrokeWidth(lineWidth);
        } catch (Exception ex) { cv = null; drawingFrame = null; return false; }
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
        float ow = ps.getStrokeWidth(); ps.setStrokeWidth(w * lineWidth / 2.5f);
        cv.drawLine(x1, y1, x2, y2, ps); ps.setStrokeWidth(ow);
    }
    static void cr(float x, float y, float radius, int color) {
        if (cv == null || !Float.isFinite(radius) || radius <= 0) return;
        float old = ps.getStrokeWidth(); ps.setStrokeWidth(2.0f); ps.setColor(color);
        cv.drawCircle(x, y, radius, ps); ps.setStrokeWidth(old);
    }
    static void t(String txt, float x, float y, int c) {
        if (cv == null) return; pt.setColor(c); cv.drawText(txt, x, y, pt);
    }
    // Actor labels follow projected actor height, independently of menu/debug text.
    static void ta(String txt, float centerX, float baseline, int color, float size) {
        if (cv == null || txt == null || !Float.isFinite(size)) return;
        if (actorText == null) {
            actorText = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
            actorText.setTypeface(Typeface.create("sans-serif", Typeface.BOLD));
            actorText.setHinting(Paint.HINTING_ON);
            actorOutline = new Paint(actorText);
            actorOutline.setStyle(Paint.Style.STROKE);
            actorOutline.setStrokeJoin(Paint.Join.ROUND);
            actorOutline.setStrokeCap(Paint.Cap.ROUND);
        }
        float fontSize = Math.max(12f, Math.min(24f, size));
        actorText.setTextSize(fontSize);
        actorText.setColor(color);
        actorOutline.setTextSize(fontSize);
        actorOutline.setColor(color & 0xFF000000);
        actorOutline.setStrokeWidth(Math.max(1.5f, Math.min(2.5f, fontSize * 0.12f)));
        float x = Math.round(centerX - actorText.measureText(txt) * 0.5f);
        float y = Math.round(baseline);
        cv.drawText(txt, x, y, actorOutline);
        cv.drawText(txt, x, y, actorText);
    }
    static void dt(String txt, float x, float y, int c) {
        if (cv == null) return;
        float size = pt.getTextSize(); pt.setTextSize(17); pt.setColor(c);
        int length = pt.breakText(txt, true, Math.max(32, sw - 24), null);
        if (length < txt.length()) txt = txt.substring(0, Math.max(0, length - 3)) + "...";
        float width = pt.measureText(txt);
        x = Math.max(8, Math.min(x, sw - width - 8)); y = Math.max(20, Math.min(y, sh - 8));
        pf.setColor(0xC0101825); cv.drawRect(x - 3, y - 18, x + width + 3, y + 4, pf);
        cv.drawText(txt, x, y, pt); pt.setTextSize(size);
    }
    static void e() {
        Frame complete = drawingFrame;
        if (cv == null || complete == null) return;
        cv = null; drawingFrame = null;
        complete.presentedAt = SystemClock.uptimeMillis();
        front = complete.bitmap; frontCanvas = complete.canvas;
        frameAt = complete.presentedAt;
        publishedFrame = complete;
        if (inst != null) inst.postInvalidateOnAnimation();
    }

    static int gw() { return sw; }
    static int gh() { return sh; }
    static boolean ok() { return rdy; }

    /* Diagnostic updates */
    static void dg(String gw) { diagGWorld = gw; refreshMenu(); }
    static void de(String ent) { diagEntities = ent; refreshMenu(); }
    static void df(String fn) { diagFName = fn; refreshMenu(); }
    static void ds(String st) { diagStatus = st; refreshMenu(); }
    static void dx(String ex) { diagExtra = ex; refreshMenu(); }
    static void dl(String el) { diagEntList = el; refreshMenu(); }

    /* Per-entity-type queries (called from C) */
    static boolean ge() { return espEnabled; }
    static int grd() { return rayDebug ? 1 | (rayViews ? 2 : 0) | (rayLos ? 4 : 0) | (rayBounds ? 8 : 0) : 0; }
    static int gf(int type) { if(type==ET_BOT_PLAYER)type=ET_ENEMY; return (type >= 0 && type < ET_COUNT) ? entityFeatures[type] : 0; }
    static int gr(int type) { if(type==ET_BOT_PLAYER)type=ET_ENEMY; return (type >= 0 && type < ET_COUNT) ? entityRenderDist[type] : 500; }
    static int gso() { return snapOrigin; }
    static int gc(int slot) {
        return (slot >= 0 && slot < ET_COUNT) ? entityColors[slot] : 0xFFE5534B;
    }
    static int gvc(int slot, boolean visible) {
        if(slot==ET_BOT_PLAYER)slot=ET_ENEMY;
        if (slot < 0 || slot >= ET_COUNT) return visible ? VISIBLE_COLOR : OCCLUDED_COLOR;
        return visible ? visibleColors[slot] : occludedColors[slot];
    }
    static void setVisibilityColor(int slot, boolean visible, int color) {
        if (slot >= 0 && slot < ET_COUNT)
            (visible ? visibleColors : occludedColors)[slot] = color;
    }

    /* Aim queries (called from C) */
    static boolean ga() { return aimEnabled; }
    static int gaf() { return aimFov; }
    static int gas() { return aimStrength; }
    static int gab() { return aimBone; }
    static boolean gaa() { return autoShoot; }
    static boolean gaz() { return autoZoom; }
    static int gam() { return aimTargetMask; }
    static boolean gms() { return menuShown; }
    static boolean gal() { return aimLocked; }
    static boolean ga3() { return aim360; }
    static int gap() { return aimPriority; }
    static int gad() { return aimMaxDist; }
    static int gtp() { return aimTypePref; }

    /* Misc queries (called from C) */
    static boolean gnr() { return noRecoil; }
    static boolean gns() { return noSpread; }
    static boolean gnw() { return noWeaponSway; }
    static boolean gis() { return infiniteStamina; }
    static int gcf() { return cameraFov; }
    static boolean gfp() { return forceTPP; }

    /* Chams queries (called from C) */
    static boolean gce() { return chamsEnabled; }
    static int gcv() { return chamsVisibleColor; }
    static int gci() { return chamsInvisibleColor; }
    static int gcb() { return chamsBrightness; }
    static int glr() { return lootRarityMask; }
    static int glm() { return lootMinValue; }
    static int glf() { return (lootShowValue ? 1 : 0) | (lootShowContainers ? 2 : 0); }
    static void lc(final int quality, final int color) {
        if (quality < 0 || quality > 7 || lootGameColors[quality] == color) return;
        int[] next = lootGameColors.clone(); next[quality] = color; lootGameColors = next;
        if (uiHandler != null) uiHandler.post(new Runnable() {
            public void run() {
                if (menu == null || menu.selectedTab != 5) return;
                recolorLootRows(menu,quality,color);
            }
        });
    }

    static void setColor(int slot, int color) {
        if (slot >= 0 && slot < ET_COUNT) entityColors[slot] = color;
    }
    static void recolorLootRows(View view,int quality,int color) {
        if(view instanceof Switch && Integer.valueOf(quality).equals(view.getTag())) ((Switch)view).setTextColor(color);
        if(view instanceof ViewGroup)for(int i=0;i<((ViewGroup)view).getChildCount();i++)recolorLootRows(((ViewGroup)view).getChildAt(i),quality,color);
    }

    static void saveSettings() {
        if (prefs == null) return;
        SharedPreferences.Editor edit = prefs.edit().putBoolean("esp", espEnabled)
            .putFloat("width", lineWidth).putInt("menuX", menuX).putInt("menuY", menuY)
            .putInt("snapOrigin", snapOrigin)
            .putBoolean("aim", aimEnabled).putInt("aimFov", aimFov)
            .putInt("aimStr", aimStrength).putInt("aimBone", aimBone)
            .putBoolean("autoShoot", autoShoot).putBoolean("autoZoom", autoZoom).putBoolean("showLockBtn", showLockBtn).putBoolean("aim360", aim360).putInt("aimMask", aimTargetMask).putInt("aimPriority", aimPriority).putInt("aimMaxDist", aimMaxDist).putInt("aimTypePref", aimTypePref)
            .putBoolean("rayDebug", rayDebug).putBoolean("rayViews", rayViews)
            .putBoolean("rayLos", rayLos).putBoolean("rayBounds", rayBounds)
            .putBoolean("noRecoil", noRecoil).putBoolean("noSpread", noSpread).putBoolean("noWeaponSway", noWeaponSway).putBoolean("infiniteStamina", infiniteStamina)
            .putInt("cameraFov", cameraFov).putBoolean("forceTPP", forceTPP)
            .putInt("chamsBrightness", chamsBrightness).putBoolean("chamsEnabled", chamsEnabled).putInt("chamsVisibleColor", chamsVisibleColor).putInt("chamsInvisibleColor", chamsInvisibleColor)
            .putInt("lootRarityMask", lootRarityMask).putInt("lootMinValue", lootMinValue)
            .putBoolean("lootShowValue", lootShowValue).putBoolean("lootShowContainers", lootShowContainers);
        for (int i = 0; i < ET_COUNT; i++) {
            edit.putInt(entityKey("color", i), entityColors[i]);
            edit.putInt(entityKey("visibleColor", i), visibleColors[i]);
            edit.putInt(entityKey("occludedColor", i), occludedColors[i]);
            edit.putInt(entityKey("feat", i), entityFeatures[i]);
            edit.putInt(entityKey("rdist", i), entityRenderDist[i]);
        }
        edit.apply();
    }

    static File configRoot;
    static void loadSettings() {
    espEnabled = prefs.getBoolean("esp", true);
    rayDebug = prefs.getBoolean("rayDebug", false);
    rayViews = prefs.getBoolean("rayViews", true);
    rayLos = prefs.getBoolean("rayLos", true);
    rayBounds = prefs.getBoolean("rayBounds", true);
    lineWidth = Math.max(1,Math.min(5,prefs.getFloat("width", 2.5f)));
    menuX = prefs.getInt("menuX", -1); menuY = prefs.getInt("menuY", -1);
    snapOrigin = prefs.getInt("snapOrigin", 0);
    for (int i = 0; i < ET_COUNT; i++) {
        entityColors[i] = prefs.getInt(entityKey("color", i), defaultColors[i]);
        visibleColors[i] = prefs.getInt(entityKey("visibleColor", i), VISIBLE_COLOR);
        occludedColors[i] = prefs.getInt(entityKey("occludedColor", i), OCCLUDED_COLOR);
        entityFeatures[i] = prefs.getInt(entityKey("feat", i), defaultFeatures(i)) & 127;
        entityRenderDist[i] = Math.max(10,Math.min(1000,prefs.getInt(entityKey("rdist", i), 500)));
    }
    aimEnabled = prefs.getBoolean("aim", false);
    aimFov = Math.max(5, Math.min(45, prefs.getInt("aimFov", 15)));
    aimStrength = prefs.getInt("aimStr", 100);
    aimBone = Math.max(0, Math.min(2, prefs.getInt("aimBone", 2)));
    autoShoot = prefs.getBoolean("autoShoot", false);
    autoZoom = prefs.getBoolean("autoZoom", false);
    showLockBtn = prefs.getBoolean("showLockBtn", false);
    aim360 = prefs.getBoolean("aim360", false);
    aimTargetMask = prefs.getInt("aimMask", AIM_ENEMIES | AIM_BOTS) & AIM_ALL;
    aimPriority = Math.max(0,Math.min(3,prefs.getInt("aimPriority", 0)));
    aimMaxDist = Math.max(0,Math.min(500,prefs.getInt("aimMaxDist", 0)));
    aimTypePref = Math.max(0,Math.min(3,prefs.getInt("aimTypePref", 0)));
    noRecoil = prefs.getBoolean("noRecoil", false);
    noSpread = prefs.getBoolean("noSpread", false);
    noWeaponSway = prefs.getBoolean("noWeaponSway", false);
    infiniteStamina = prefs.getBoolean("infiniteStamina", false);
    forceTPP = prefs.getBoolean("forceTPP", false);
    lootRarityMask = prefs.getInt("lootRarityMask", 255) & 255;
    lootMinValue = Math.max(0, Math.min(1000000, prefs.getInt("lootMinValue", 0)));
    lootShowValue = prefs.getBoolean("lootShowValue", true);
    lootShowContainers = prefs.getBoolean("lootShowContainers", true);
    cameraFov = prefs.getInt("cameraFov", 0);
    if(cameraFov!=0) cameraFov=Math.max(10,Math.min(170,cameraFov));
    chamsEnabled = prefs.getBoolean("chamsEnabled", false);
    chamsBrightness = Math.max(10, Math.min(100, prefs.getInt("chamsBrightness", 70)));
    chamsVisibleColor = prefs.getInt("chamsVisibleColor", 0xFF00FF00);
    chamsInvisibleColor = prefs.getInt("chamsInvisibleColor", 0xFFFF0000);

        updateLockBtnVisibility();
    }

    static JSONObject settingsSnapshot() throws Exception {
        if(prefs==null)throw new IOException("Settings are not ready yet");
        saveSettings();JSONObject settings=new JSONObject();
        for(Map.Entry<String,?> entry:prefs.getAll().entrySet())
            if(!entry.getKey().equals("menuX")&&!entry.getKey().equals("menuY"))settings.put(entry.getKey(),entry.getValue());
        return settings;
    }
    static void applyConfig(JSONObject settings) throws Exception {
        settingsSnapshot();Map<String,?> known=prefs.getAll();java.util.LinkedHashMap<String,Object> validated=new java.util.LinkedHashMap<>();
        java.util.Iterator<String> keys=settings.keys();
        while(keys.hasNext()) {
            String key=keys.next();if(key.equals("menuX")||key.equals("menuY")||!known.containsKey(key))continue;
            Object expected=known.get(key),value=settings.get(key);
            if(expected instanceof Boolean){if(!(value instanceof Boolean))throw new IOException("Invalid value for "+key);validated.put(key,value);}
            else if(expected instanceof Integer){if(!(value instanceof Number))throw new IOException("Invalid number for "+key);double n=((Number)value).doubleValue();if(!Double.isFinite(n)||n!=Math.rint(n)||n<Integer.MIN_VALUE||n>Integer.MAX_VALUE)throw new IOException("Invalid number for "+key);validated.put(key,(int)n);}
            else if(expected instanceof Float){if(!(value instanceof Number)||!Double.isFinite(((Number)value).doubleValue())||Math.abs(((Number)value).doubleValue())>Float.MAX_VALUE)throw new IOException("Invalid number for "+key);validated.put(key,((Number)value).floatValue());}
        }
        if(validated.isEmpty())throw new IOException("No supported settings in this file");
        SharedPreferences.Editor edit=prefs.edit();
        for(Map.Entry<String,Object> entry:validated.entrySet()){
            Object value=entry.getValue();if(value instanceof Boolean)edit.putBoolean(entry.getKey(),(Boolean)value);else if(value instanceof Integer)edit.putInt(entry.getKey(),(Integer)value);else if(value instanceof Float)edit.putFloat(entry.getKey(),(Float)value);
        }
        if(!edit.commit())throw new IOException("Could not load settings");loadSettings();
    }
    static class ConfigFiles {
        static File directory() throws IOException {
            if(configRoot==null)throw new IOException("Config storage is not ready");
            if(!configRoot.isDirectory()&&!configRoot.mkdirs())throw new IOException("Could not open config storage");return configRoot.getCanonicalFile();
        }
        static File checked(File file) throws IOException {
            if(file==null||!file.getCanonicalFile().getParentFile().equals(directory())||!file.getName().endsWith(".json"))throw new IOException("Invalid config file");return file;
        }
        static JSONObject read(File file) throws Exception {
            checked(file);if(file.length()<2||file.length()>131072)throw new IOException("Config file is empty or too large");
            byte[] bytes=new byte[(int)file.length()];int offset=0;
            try(FileInputStream in=new FileInputStream(file)){while(offset<bytes.length){int n=in.read(bytes,offset,bytes.length-offset);if(n<0)throw new IOException("Incomplete config file");offset+=n;}}
            JSONObject config=new JSONObject(new String(bytes,java.nio.charset.StandardCharsets.UTF_8));
            if(config.getInt("version")!=1)throw new IOException("Unsupported config version");config.put("name",name(config.getString("name")));config.getJSONObject("settings");return config;
        }
        static String name(String raw) throws IOException {
            String name=raw.trim();if(name.isEmpty()||name.length()>40)throw new IOException("Use a name between 1 and 40 characters");
            for(int i=0;i<name.length();i++)if(Character.isISOControl(name.charAt(i)))throw new IOException("Config names cannot contain control characters");return name;
        }
        static File[] list() throws IOException {
            File[] files=directory().listFiles(new java.io.FilenameFilter(){public boolean accept(File dir,String name){return name.endsWith(".json");}});
            if(files==null)throw new IOException("Could not read config list");java.util.Arrays.sort(files,new java.util.Comparator<File>(){public int compare(File a,File b){return Long.compare(b.lastModified(),a.lastModified());}});return files;
        }
        static File write(File target,String raw,JSONObject settings) throws Exception {
            String name=name(raw);File root=directory();if(target!=null)checked(target);
            for(File file:list())if(!file.equals(target)){
                String existing=null;try{existing=read(file).getString("name");}catch(Exception invalid){/* Preserve malformed files, but allow saving other profiles. */}
                if(name.equalsIgnoreCase(existing))throw new IOException("A config with that name already exists");
            }
            File previous=target;
            String filename="config-"+name.replaceAll("[^\\p{L}\\p{N} _.-]","_")+".json";
            target=new File(root,filename);checked(target);
            if(target.exists()&&!target.equals(previous))throw new IOException("A config with that file name already exists");
            JSONObject config=new JSONObject();config.put("version",1);config.put("name",name);config.put("settings",settings);
            File temporary=File.createTempFile("profile-",".tmp",root);
            try {
                try(FileOutputStream out=new FileOutputStream(temporary)){out.write(config.toString(2).getBytes(java.nio.charset.StandardCharsets.UTF_8));out.getFD().sync();}
                if(!temporary.renameTo(target))throw new IOException("Could not save config");
                if(previous!=null&&!previous.equals(target)&&!previous.delete()){target.delete();throw new IOException("Could not rename config");}
            } finally {if(temporary.exists())temporary.delete();}
            return target;
        }
        static void delete(File file) throws IOException {checked(file);if(!file.delete())throw new IOException("Could not delete config");}
    }

    static volatile boolean menuShown = false;

    static void toggleMenu() {
        if (wm == null || menu == null || bubble == null) return;
        if (menuShown) {
            try { wm.removeView(menu); } catch (Exception ex) {}
            menuShown = false;
        } else {
            WindowManager.LayoutParams mlp = new WindowManager.LayoutParams(
                Math.min(1520, Math.max(320, viewWidth - 64)),
                Math.min(940, Math.max(320, viewHeight - 64)),
                WindowManager.LayoutParams.TYPE_APPLICATION,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                PixelFormat.TRANSLUCENT);
            WindowManager.LayoutParams blp = (WindowManager.LayoutParams) bubble.getLayoutParams();
            mlp.gravity = Gravity.TOP | Gravity.LEFT;
            mlp.x = clampPosition(menuX < 0 ? (viewWidth - mlp.width) / 2 : menuX, viewWidth, mlp.width);
            mlp.y = clampPosition(menuY < 0 ? (viewHeight - mlp.height) / 2 : menuY, viewHeight, mlp.height);
            wm.addView(menu, mlp);
            menuShown = true;
        }
        bubble.invalidate();
    }

    static int clampPosition(int position, int screen, int size) {
        return Math.max(0, Math.min(position, Math.max(0, screen - size)));
    }

    static void refreshMenu() {
        if (menuShown && menu != null) {
            if (uiHandler != null) uiHandler.post(new Runnable() {
                public void run() { if (menu != null && menuShown) menu.refreshStatus(); }
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
            cp.setStyle(Paint.Style.FILL);cp.setColor(0xF0171A26);c.drawRoundRect(new RectF(3,3,getWidth()-3,getHeight()-3),22,22,cp);
            cp.setStyle(Paint.Style.STROKE);cp.setStrokeWidth(2);cp.setColor(menuShown?0xFFAD9DFF:0xFF555071);c.drawRoundRect(new RectF(3,3,getWidth()-3,getHeight()-3),22,22,cp);
            cp.setStyle(Paint.Style.FILL);tp.setColor(0xFFD8D0FF);c.drawText("D",cx,cy+11,tp);
            cp.setColor(menuShown?0xFFB8A9FF:0xFF7DD9AE);c.drawCircle(getWidth()-15,15,4,cp);
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

    /* ======== Lock toggle button ======== */
    static class LockBtnView extends View {
        Paint cp, tp;
        LockBtnView(Context c) {
            super(c);
            cp = new Paint(Paint.ANTI_ALIAS_FLAG);
            tp = new Paint(Paint.ANTI_ALIAS_FLAG);
            tp.setColor(0xFFFFFFFF);
            tp.setTextSize(24f);
            tp.setTypeface(Typeface.DEFAULT_BOLD);
            tp.setTextAlign(Paint.Align.CENTER);
        }
        @Override protected void onDraw(Canvas c) {
            float cx = getWidth() / 2f, cy = getHeight() / 2f, r = Math.min(cx, cy) - 2;
            cp.setColor(aimLocked ? 0xDD7560D0 : 0xDD242938);
            c.drawCircle(cx, cy, r, cp);
            c.drawText(aimLocked ? "◎" : "○", cx, cy + 9, tp);
        }
        @Override public boolean onTouchEvent(MotionEvent ev) {
            if (ev.getAction() == MotionEvent.ACTION_UP) {
                aimLocked = !aimLocked;
                invalidate();
            }
            return true;
        }
    }
    static void updateLockBtnVisibility() {
        if (uiHandler == null) return;
        uiHandler.post(new Runnable() { public void run() {
            if (lockBtn == null || wm == null) return;
            boolean show = showLockBtn && aimEnabled;
            lockBtn.setVisibility(show ? View.VISIBLE : View.GONE);
            lockBtn.invalidate();
        }});
    }

    /* ======== Menu panel with tabs ======== */
    static class MenuPanel extends LinearLayout {
        static final int INK = 0xFFE9ECF4, MUTED = 0xFF8E95A9, ACCENT = 0xFF9C8CFF;
        static final int BG_DARK = 0xFC11131B, BG_CARD = 0xFF191D28, BG_ACTIVE = 0xFF34304F;
        static final int BORDER = 0xFF2A3040;
        LinearLayout body, nav;
        final LinearLayout[] columns = new LinearLayout[2];
        final ScrollView[] scrolls = new ScrollView[2];
        final int[][] scrollPositions = new int[8][2];
        TextView status, summary, dragHandle, pageTitle;
        int selectedTab = -1, selectedEntityType, colorSlot, nextCard;
        boolean colorVisible = true;
        final String[] colorNames = ET_NAMES;

        MenuPanel(Context context) {
            super(context);
            setOrientation(HORIZONTAL); setPadding(0, 0, 0, 0);
            setDescendantFocusability(FOCUS_BLOCK_DESCENDANTS);
            setBackground(surface(BG_DARK, 18)); setClipToOutline(true);
            LinearLayout sidebar = new LinearLayout(context); sidebar.setOrientation(VERTICAL);
            sidebar.setPadding(14, 24, 14, 18); sidebar.setBackgroundColor(0xFF0D1017);
            addView(sidebar,new LayoutParams(216,-1));
            LinearLayout brand = row();brand.setGravity(Gravity.CENTER_VERTICAL);
            TextView mark=text("D",23,ACCENT);mark.setGravity(Gravity.CENTER);mark.setTypeface(Typeface.DEFAULT_BOLD);
            mark.setBackground(surface(0xFF26213D,12));brand.addView(mark,new LayoutParams(40,40));
            LinearLayout wordmark=new LinearLayout(context);wordmark.setOrientation(VERTICAL);wordmark.setPadding(12,0,0,0);
            TextView name=text("DELTA",23,INK);name.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));wordmark.addView(name);
            TextView edition=text("CONTROL PANEL",10,MUTED);edition.setLetterSpacing(.12f);wordmark.addView(edition);
            brand.addView(wordmark);sidebar.addView(brand,new LayoutParams(-1,62));installDrag(brand);
            TextView workspace=text("WORKSPACE",12,0xFF636C82);workspace.setLetterSpacing(.12f);
            workspace.setPadding(10,20,0,14);sidebar.addView(workspace,new LayoutParams(-1,60));
            nav=new LinearLayout(context);nav.setOrientation(VERTICAL);sidebar.addView(nav,new LayoutParams(-1,-2));
            sidebar.addView(new View(context),new LayoutParams(1,0,1));
            View divider=new View(context);divider.setBackgroundColor(BORDER);sidebar.addView(divider,new LayoutParams(-1,1));
            TextView session=text("●  ON DEVICE",13,0xFF84D4B3);session.setPadding(8,14,0,0);sidebar.addView(session,new LayoutParams(-1,42));
            TextView saved=text("Settings save automatically",12,MUTED);saved.setPadding(8,0,0,0);sidebar.addView(saved,new LayoutParams(-1,26));

            LinearLayout main=new LinearLayout(context);main.setOrientation(VERTICAL);main.setPadding(24,20,24,14);
            addView(main,new LayoutParams(0,-1,1));
            LinearLayout header=row();header.setGravity(Gravity.CENTER_VERTICAL);
            pageTitle=text("Visuals",28,INK);pageTitle.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));
            dragHandle=pageTitle;pageTitle.setContentDescription("Drag menu");installDrag(pageTitle);
            header.addView(pageTitle,new LayoutParams(0,52,1));
            TextView close=text("×",28,MUTED);close.setGravity(Gravity.CENTER);close.setContentDescription("Close menu");
            close.setBackground(new RippleDrawable(ColorStateList.valueOf(0x339C8CFF),surface(0xFF1E2230,10),null));
            header.addView(close,new LayoutParams(42,42));close.setOnClickListener(new OnClickListener(){public void onClick(View v){toggleMenu();}});
            main.addView(header,new LayoutParams(-1,48));
            summary=text("",16,MUTED);main.addView(summary,new LayoutParams(-1,46));
            LinearLayout content=row();main.addView(content,new LayoutParams(-1,0,1));
            for(int i=0;i<2;i++) {
                ScrollView scroll=new ScrollView(context);scroll.setFillViewport(false);scroll.setVerticalScrollBarEnabled(false);
                scroll.setOverScrollMode(OVER_SCROLL_NEVER);scrolls[i]=scroll;
                columns[i]=new LinearLayout(context);columns[i].setOrientation(VERTICAL);columns[i].setPadding(0,0,0,12);
                scroll.addView(columns[i],new ScrollView.LayoutParams(-1,-2));
                LayoutParams lp=new LayoutParams(0,-1,1);if(i==0)lp.setMargins(0,0,16,0);content.addView(scroll,lp);
            }
            TextView footer=text("Drag the title to move  ·  Scroll each column for more settings",12,0xFF626B81);
            main.addView(footer,new LayoutParams(-1,28));selectTab(0);
        }
        static class MenuIcon extends View {
            final int kind,color;final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
            MenuIcon(Context context,int kind,int color){super(context);this.kind=kind;this.color=color;}
            @Override protected void onDraw(Canvas canvas){
                canvas.save();canvas.scale(getWidth()/24f,getHeight()/24f);paint.setColor(color);paint.setStrokeWidth(1.7f);
                paint.setStrokeCap(Paint.Cap.ROUND);paint.setStrokeJoin(Paint.Join.ROUND);paint.setStyle(Paint.Style.STROKE);
                if(kind==0){Path eye=new Path();eye.moveTo(2,12);eye.cubicTo(7,3,17,3,22,12);eye.cubicTo(17,21,7,21,2,12);canvas.drawPath(eye,paint);canvas.drawCircle(12,12,3,paint);}
                else if(kind==2){canvas.drawCircle(12,12,7,paint);canvas.drawCircle(12,12,2,paint);canvas.drawLine(12,1,12,6,paint);canvas.drawLine(12,18,12,23,paint);canvas.drawLine(1,12,6,12,paint);canvas.drawLine(18,12,23,12,paint);}
                else if(kind==6){Path cube=new Path();cube.moveTo(12,2);cube.lineTo(21,7);cube.lineTo(21,17);cube.lineTo(12,22);cube.lineTo(3,17);cube.lineTo(3,7);cube.close();canvas.drawPath(cube,paint);canvas.drawLine(3,7,12,12,paint);canvas.drawLine(21,7,12,12,paint);canvas.drawLine(12,12,12,22,paint);}
                else if(kind==5){canvas.drawRoundRect(new RectF(4,7,20,21),3,3,paint);canvas.drawArc(new RectF(8,1,16,13),180,180,false,paint);canvas.drawLine(4,12,20,12,paint);canvas.drawLine(10,12,14,12,paint);}
                else if(kind==7){Path folder=new Path();folder.moveTo(3,6);folder.lineTo(10,6);folder.lineTo(12,9);folder.lineTo(21,9);folder.lineTo(21,21);folder.lineTo(3,21);folder.close();canvas.drawPath(folder,paint);}
                else if(kind==3){for(int i=0;i<3;i++){float y=5+i*7;canvas.drawLine(3,y,21,y,paint);canvas.drawCircle(i==1?8:16,y,2.6f,paint);}}
                else if(kind==1){canvas.drawCircle(12,12,9,paint);paint.setStyle(Paint.Style.FILL);canvas.drawCircle(9,7,1.5f,paint);canvas.drawCircle(16,8,1.5f,paint);canvas.drawCircle(7,13,1.5f,paint);canvas.drawCircle(13,16,2.8f,paint);}
                else {Path pulse=new Path();pulse.moveTo(2,13);pulse.lineTo(7,13);pulse.lineTo(10,5);pulse.lineTo(14,20);pulse.lineTo(17,11);pulse.lineTo(22,11);canvas.drawPath(pulse,paint);}
                canvas.restore();
            }
        }
        class EspPreview extends View {
            final boolean chams;final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
            EspPreview(Context context,boolean chams){super(context);this.chams=chams;setContentDescription("Illustrative overlay preview");}
            void label(Canvas c,String value,float x,float y,int color,int size){paint.setStyle(Paint.Style.FILL);paint.setColor(color);paint.setTextSize(size);paint.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));paint.setTextAlign(Paint.Align.CENTER);c.drawText(value,x,y,paint);}
            void mannequin(Canvas c,float cx,float top,float scale,int color){
                c.save();c.translate(cx,top);c.scale(scale,scale);paint.setColor(color);paint.setStyle(Paint.Style.FILL);
                c.drawCircle(0,18,15,paint);Path torso=new Path();torso.moveTo(-24,43);torso.lineTo(24,43);torso.lineTo(20,118);torso.lineTo(-20,118);torso.close();c.drawPath(torso,paint);
                paint.setStrokeWidth(18);paint.setStrokeCap(Paint.Cap.ROUND);
                c.drawLine(-23,52,-37,94,paint);c.drawLine(-37,94,-31,127,paint);c.drawLine(23,52,37,94,paint);c.drawLine(37,94,31,127,paint);
                c.drawLine(-12,117,-15,161,paint);c.drawLine(-15,161,-18,208,paint);c.drawLine(12,117,15,161,paint);c.drawLine(15,161,18,208,paint);
                paint.setStrokeWidth(12);c.drawLine(-18,208,-27,211,paint);c.drawLine(18,208,27,211,paint);c.restore();
            }
            int modelColor(int color){float value=chamsBrightness/100f;return Color.rgb(Math.round(Color.red(color)*value),Math.round(Color.green(color)*value),Math.round(Color.blue(color)*value));}
            @Override protected void onDraw(Canvas c){
                float w=getWidth(),h=getHeight(),cx=w*.5f,scale=(h-88)/220f,top=42;
                int type=selectedTab==1?colorSlot:selectedEntityType;if(type==ET_LOOT)type=ET_ENEMY;
                int color=gvc(type,true),flags=entityFeatures[type];
                paint.setStyle(Paint.Style.FILL);paint.setColor(0xFF121620);c.drawRoundRect(new RectF(0,0,w,h),10,10,paint);
                paint.setColor(0xFF1B2130);paint.setStrokeWidth(1);
                for(int x=24;x<w;x+=28)c.drawLine(x,32,x,h-28,paint);for(int y=32;y<h-28;y+=28)c.drawLine(12,y,w-12,y,paint);
                label(c,chams?"VISIBLE  /  BEHIND COVER":ET_NAMES[type].toUpperCase(java.util.Locale.ROOT),cx,23,0xFF7E8AA2,11);
                mannequin(c,cx,top,scale,chams&&chamsEnabled?modelColor(chamsVisibleColor):0xFF394355);
                if(chams){
                    paint.setColor(0xD5232936);c.drawRoundRect(new RectF(14,h*.58f,w-14,h-30),7,7,paint);
                    if(chamsEnabled){c.save();c.clipRect(14,h*.58f,w-14,h-30);mannequin(c,cx,top,scale,modelColor(chamsInvisibleColor));c.restore();}
                    label(c,"Cover",w-43,h*.58f+19,0xFF69768D,11);
                }else if(espEnabled && (flags&F_ENABLED)!=0){
                    float left=cx-48*scale,right=cx+48*scale,bottom=top+220*scale;
                    paint.setColor(color);paint.setStrokeWidth(Math.max(1,lineWidth*.7f));paint.setStyle(Paint.Style.STROKE);
                    if((flags&F_BOXES)!=0)c.drawRect(left,top,right,bottom,paint);
                    if((flags&F_SKELETON)!=0){float[] joints={0,19,0,54, 0,54,0,116, -25,54,25,54, -25,54,-36,92, -36,92,-31,126,25,54,36,92,36,92,31,126,0,116,-14,160,-14,160,-18,209,0,116,14,160,14,160,18,209};c.save();c.translate(cx,top);c.scale(scale,scale);c.drawLines(joints,paint);c.restore();}
                    paint.setStyle(Paint.Style.FILL);
                    if((flags&F_HEALTH)!=0){paint.setColor(0xFF303846);c.drawRect(left-8,top,left-5,bottom,paint);paint.setColor(0xFF75D7AA);c.drawRect(left-8,top+24,left-5,bottom,paint);}
                    if((flags&F_SNAPLINES)!=0){paint.setColor(color);paint.setStrokeWidth(1);c.drawLine(cx,snapOrigin==0?h:0,cx,snapOrigin==0?bottom:top,paint);}
                    if((flags&F_NAMES)!=0)label(c,"Operator",cx,top-8,color,13);
                    if((flags&F_DISTANCE)!=0)label(c,"24 m",cx,bottom+17,color,12);
                }
                label(c,"ILLUSTRATIVE PREVIEW",cx,h-10,0xFF4D586E,9);
            }
        }

        File selectedConfig;
        String configNotice="",nameDraft="",nameMode;
        boolean nameCaps,confirmDelete;
        TextView nameEntry;
        TextView action(String label,final Runnable callback) {
            TextView button=text(label,18,INK);button.setGravity(Gravity.CENTER);button.setBackground(new RippleDrawable(ColorStateList.valueOf(0x309C8CFF),surface(0xFF292D3D,10),null));
            button.setOnClickListener(new OnClickListener(){public void onClick(View v){callback.run();}});return button;
        }
        void configError(Exception error){configNotice=error.getMessage()==null?"Could not update config":error.getMessage();selectTab(7);}
        void buildConfigsTab() {
            sectionIn("Saved configs","Load a setup, adjust settings in the menu, then save your changes.",0);
            if(!configNotice.isEmpty()){TextView notice=text(configNotice,17,ACCENT);body.addView(notice,new LayoutParams(-1,48));}
            try {
                File[] files=ConfigFiles.list();if(selectedConfig==null&&files.length>0)selectedConfig=files[0];
                if(files.length==0)body.addView(text("No saved configs yet. Create one from your current settings.",19,MUTED),new LayoutParams(-1,80));
                for(int i=0;i<Math.min(files.length,128);i++){
                    final File file=files[i];String name;try{name=ConfigFiles.read(file).getString("name");}catch(Exception error){name=file.getName()+"  ·  Unreadable";}
                    TextView item=text(name,21,file.equals(selectedConfig)?INK:MUTED);item.setSingleLine(true);item.setEllipsize(android.text.TextUtils.TruncateAt.END);item.setPadding(16,0,16,0);item.setBackground(surface(file.equals(selectedConfig)?BG_ACTIVE:0xFF222735,10));
                    LayoutParams lp=new LayoutParams(-1,58);lp.setMargins(0,0,0,8);body.addView(item,lp);
                    item.setOnClickListener(new OnClickListener(){public void onClick(View v){selectedConfig=file;nameMode=null;confirmDelete=false;configNotice="";selectTab(7);}});
                }
            }catch(Exception error){body.addView(text("Config storage will be ready when the game finishes loading.",18,MUTED),new LayoutParams(-1,70));}
            LinearLayout controls=row();body.addView(controls,new LayoutParams(-1,58));
            TextView create=action("New from current",new Runnable(){public void run(){nameDraft="";nameMode="create";configNotice="";selectTab(7);}});controls.addView(create,new LayoutParams(-1,52));
            if(selectedConfig!=null){
                sectionIn("Selected config",null,0);
                LinearLayout primary=row();LayoutParams half=new LayoutParams(0,54,1);half.setMargins(0,0,8,0);
                primary.addView(action("Load",new Runnable(){public void run(){try{JSONObject file=ConfigFiles.read(selectedConfig);applyConfig(file.getJSONObject("settings"));configNotice="Loaded "+file.getString("name");nameMode=null;selectTab(7);}catch(Exception error){configError(error);}}}),half);
                primary.addView(action("Save changes",new Runnable(){public void run(){try{String name=ConfigFiles.read(selectedConfig).getString("name");selectedConfig=ConfigFiles.write(selectedConfig,name,settingsSnapshot());configNotice="Saved "+name;selectTab(7);}catch(Exception error){configError(error);}}}),new LayoutParams(0,54,1));body.addView(primary,new LayoutParams(-1,64));
                LinearLayout secondary=row();secondary.addView(action("Rename",new Runnable(){public void run(){try{nameDraft=ConfigFiles.read(selectedConfig).getString("name");nameMode="rename";configNotice="";selectTab(7);}catch(Exception error){configError(error);}}}),half);
                secondary.addView(action(confirmDelete?"Confirm delete":"Delete",new Runnable(){public void run(){if(!confirmDelete){confirmDelete=true;selectTab(7);return;}try{ConfigFiles.delete(selectedConfig);selectedConfig=null;confirmDelete=false;configNotice="Config deleted";selectTab(7);}catch(Exception error){configError(error);}}}),new LayoutParams(0,54,1));body.addView(secondary,new LayoutParams(-1,62));
            }
            sectionIn("Current setup",null,1);
            String summary=(espEnabled?"Visuals on":"Visuals off")+"  ·  "+(chamsEnabled?"Chams on":"Chams off")+"\n"+(aimEnabled?"Aim on":"Aim off")+"  ·  "+(aim360?"360° targeting":aimFov+"° aim radius")+"\nLoot: "+Integer.bitCount(lootRarityMask&255)+" rarity filters";
            TextView overview=text(summary,20,INK);overview.setLineSpacing(12,1);body.addView(overview,new LayoutParams(-1,130));
            if(nameMode!=null)buildNameEditor();
            else {sectionIn("How configs work",null,1);TextView note=text("New from current saves everything you have selected.\n\nTo edit a setup: load it, adjust the controls on any page, return here and choose Save changes.\n\nNames and saved setups remain on this device.",19,MUTED);note.setLineSpacing(5,1);body.addView(note,new LayoutParams(-1,-2));}
        }
        void nameChanged(){if(nameEntry!=null)nameEntry.setText(nameDraft.isEmpty()?"Config name…":nameDraft+"  │");}
        void buildNameEditor(){
            sectionIn(nameMode.equals("rename")?"Rename config":"Name your config",null,1);
            nameEntry=text("",21,INK);nameEntry.setSingleLine(true);nameEntry.setEllipsize(android.text.TextUtils.TruncateAt.START);nameEntry.setPadding(14,0,14,0);nameEntry.setBackground(surface(0xFF252338,10));body.addView(nameEntry,new LayoutParams(-1,58));nameChanged();
            String[] rows={"1234567890","qwertyuiop","asdfghjkl","zxcvbnm-_."};
            for(String keys:rows){LinearLayout row=row();for(int i=0;i<keys.length();i++){final String key=String.valueOf(keys.charAt(i));TextView button=action(nameCaps?key.toUpperCase(java.util.Locale.ROOT):key,new Runnable(){public void run(){if(nameDraft.length()<40){nameDraft+=nameCaps?key.toUpperCase(java.util.Locale.ROOT):key;nameChanged();}}});LayoutParams lp=new LayoutParams(0,52,1);lp.setMargins(2,4,2,0);row.addView(button,lp);}body.addView(row,new LayoutParams(-1,48));}
            LinearLayout extra=row();extra.addView(action("Aa",new Runnable(){public void run(){nameCaps=!nameCaps;selectTab(7);}}),new LayoutParams(0,46,1));
            LayoutParams space=new LayoutParams(0,46,3);space.setMargins(6,0,6,0);extra.addView(action("Space",new Runnable(){public void run(){if(nameDraft.length()<40){nameDraft+=" ";nameChanged();}}}),space);
            extra.addView(action("⌫",new Runnable(){public void run(){if(!nameDraft.isEmpty()){nameDraft=nameDraft.substring(0,nameDraft.offsetByCodePoints(nameDraft.length(),-1));nameChanged();}}}),new LayoutParams(0,46,1));LayoutParams extraLp=new LayoutParams(-1,54);extraLp.setMargins(0,6,0,0);body.addView(extra,extraLp);
            LinearLayout actions=row();LayoutParams left=new LayoutParams(0,54,1);left.setMargins(0,0,8,0);actions.addView(action("Cancel",new Runnable(){public void run(){nameMode=null;selectTab(7);}}),left);
            actions.addView(action(nameMode.equals("rename")?"Rename":"Save config",new Runnable(){public void run(){try{JSONObject settings=nameMode.equals("rename")?ConfigFiles.read(selectedConfig).getJSONObject("settings"):settingsSnapshot();selectedConfig=ConfigFiles.write(nameMode.equals("rename")?selectedConfig:null,nameDraft,settings);configNotice="Saved "+ConfigFiles.read(selectedConfig).getString("name");nameMode=null;selectTab(7);}catch(Exception error){configError(error);}}}),new LayoutParams(0,54,1));body.addView(actions,new LayoutParams(-1,58));
        }

        void installDrag(final View handle) {
            handle.setOnTouchListener(new OnTouchListener() {
                float downX, downY; int originalX, originalY; boolean dragging;
                public boolean onTouch(View v, MotionEvent event) {
                    if (wm == null || !(getLayoutParams() instanceof WindowManager.LayoutParams)) return false;
                    WindowManager.LayoutParams lp = (WindowManager.LayoutParams) getLayoutParams();
                    switch (event.getActionMasked()) {
                        case MotionEvent.ACTION_DOWN:
                            downX = event.getRawX(); downY = event.getRawY();
                            originalX = lp.x; originalY = lp.y; dragging = false;
                            return true;
                        case MotionEvent.ACTION_MOVE:
                            float dx = event.getRawX() - downX, dy = event.getRawY() - downY;
                            if (dx * dx + dy * dy > 64) dragging = true;
                            if (dragging) {
                                lp.x = clampPosition(originalX + Math.round(dx), viewWidth, lp.width);
                                lp.y = clampPosition(originalY + Math.round(dy), viewHeight, lp.height);
                                menuX = lp.x; menuY = lp.y;
                                wm.updateViewLayout(MenuPanel.this, lp);
                            }
                            return true;
                        case MotionEvent.ACTION_UP:
                            if (dragging) saveSettings(); else handle.performClick();
                            return true;
                        case MotionEvent.ACTION_CANCEL:
                            dragging = false; return true;
                    }
                    return false;
                }
            });
        }
        GradientDrawable shape(int color, int radius) {
            GradientDrawable bg = new GradientDrawable(); bg.setColor(color);
            bg.setCornerRadius(radius); return bg;
        }
        TextView text(String value, int size, int color) {
            TextView label = new TextView(getContext()); label.setText(value); label.setTextColor(color);
            label.setTextSize(TypedValue.COMPLEX_UNIT_PX, Math.round(size*1.18f)); label.setGravity(Gravity.CENTER_VERTICAL);
            return label;
        }
        LinearLayout row() {
            LinearLayout line = new LinearLayout(getContext()); line.setOrientation(HORIZONTAL); return line;
        }
        GradientDrawable surface(int color,int radius) {
            GradientDrawable bg=shape(color,radius);bg.setStroke(1,BORDER);return bg;
        }
        void section(String title,String subtitle) { sectionIn(title,subtitle,nextCard++%2); }
        void sectionIn(String title,String subtitle,int column) {
            LinearLayout card=new LinearLayout(getContext());card.setOrientation(VERTICAL);card.setPadding(16,12,16,12);
            card.setBackground(surface(BG_CARD,14));LayoutParams lp=new LayoutParams(-1,-2);lp.setMargins(0,0,0,14);
            columns[column].addView(card,lp);body=card;
            TextView heading=text(title.toUpperCase(java.util.Locale.ROOT),13,MUTED);heading.setLetterSpacing(.10f);
            card.addView(heading,new LayoutParams(-1,30));
            if(subtitle!=null) {
                TextView note=text(subtitle,15,0xFF778199);note.setPadding(0,0,0,8);note.setLineSpacing(3,1);
                card.addView(note,new LayoutParams(-1,-2));
            }
        }
        void selectTab(int tab) {
            if(tab<0 || tab>7)return;
            final boolean changed=selectedTab!=tab;
            if(selectedTab>=0) for(int i=0;i<2;i++) scrollPositions[selectedTab][i]=scrolls[i].getScrollY();
            selectedTab=tab;nav.removeAllViews();for(LinearLayout column:columns)column.removeAllViews();status=null;nextCard=0;body=columns[0];
            final String[] titles={"Visuals","Appearance","Aim assist","Miscellaneous","Diagnostics","Loot","Chams","Configs"};
            String[] notes={"Configure overlays by actor type.","Set visibility colors and drawing style.","Target selection, activation and firing controls.","Weapon, movement and camera settings.","Inspect the live session and entity details.","Keep useful items visible and filter the rest.","Control visible and occluded model colors.","Save named setups for different gameplay."};
            pageTitle.setText(titles[tab]);summary.setText(notes[tab]);
            int[] order={0,6,2,5,3,7,4};
            for(final int target:order) {
                boolean active=target==tab;
                LinearLayout item=row();item.setGravity(Gravity.CENTER_VERTICAL);item.setPadding(12,0,8,0);
                item.setBackground(new RippleDrawable(ColorStateList.valueOf(0x269C8CFF),(active?surface(0xFF28243C,10):shape(0xFF0D1017,10)),null));
                MenuIcon icon=new MenuIcon(getContext(),target,active?ACCENT:0xFF737E95);item.addView(icon,new LayoutParams(23,23));
                TextView label=text(target==3?"Misc":titles[target],18,active?INK:MUTED);label.setSingleLine(true);label.setPadding(12,0,0,0);item.addView(label,new LayoutParams(0,-1,1));
                item.setContentDescription(titles[target]);item.setSelected(active);
                LayoutParams lp=new LayoutParams(-1,60);lp.setMargins(0,0,0,7);nav.addView(item,lp);
                item.setOnClickListener(new OnClickListener(){public void onClick(View v){selectTab(target);}});
            }
            boolean fullWidth = (tab == 4);
            scrolls[1].setVisibility(fullWidth ? View.GONE : View.VISIBLE);
            LayoutParams col0lp = (LayoutParams) scrolls[0].getLayoutParams();
            col0lp.setMargins(0, 0, fullWidth ? 0 : 16, 0);
            scrolls[0].setLayoutParams(col0lp);
            switch(tab) {
                case 0:buildVisualsTab();break;case 1:buildColorsTab();break;case 2:buildAimTab();break;
                case 3:buildMiscTab();break;case 4:buildStatusTab();break;case 5:buildLootTab();break;case 6:buildChamsTab();break;case 7:buildConfigsTab();break;
            }
            for(int i=0;i<2;i++) {
                final int column=i;scrolls[i].post(new Runnable(){public void run(){if(selectedTab==tab)scrolls[column].scrollTo(0,scrollPositions[tab][column]);}});
                if(changed){scrolls[i].setAlpha(.45f);scrolls[i].animate().alpha(1).setDuration(140).setInterpolator(new DecelerateInterpolator()).start();}
            }
        }
        /* ---- Visuals tab ---- */
        void buildVisualsTab() {
            if(selectedEntityType>ET_NPC)selectedEntityType=ET_ENEMY;
            sectionIn("Actor filter",null,0);toggle("Enable visuals",-1,espEnabled);buildEntityTypePicker();
            final int et=selectedEntityType;int feat=entityFeatures[et];
            sectionIn("Overlay elements",ET_NAMES[et],0);
            etToggle("Enabled",F_ENABLED,(feat&F_ENABLED)!=0);etToggle("Boxes",F_BOXES,(feat&F_BOXES)!=0);
            etToggle("Skeleton",F_SKELETON,(feat&F_SKELETON)!=0);etToggle("Names & types",F_NAMES,(feat&F_NAMES)!=0);
            etToggle("Health bars",F_HEALTH,(feat&F_HEALTH)!=0);etToggle("Distance",F_DISTANCE,(feat&F_DISTANCE)!=0);
            etToggle("Snap lines",F_SNAPLINES,(feat&F_SNAPLINES)!=0);
            sectionIn("Preview",null,1);body.addView(new EspPreview(getContext(),false),new LayoutParams(-1,350));
            sectionIn("Projection",null,1);
            final TextView distance=text("Render distance  ·  "+entityRenderDist[et]+" m",18,INK);body.addView(distance,new LayoutParams(-1,38));
            SeekBar dist=new SeekBar(getContext());dist.setMax(200);dist.setProgress(entityRenderDist[et]/5);styleSlider(dist);
            body.addView(dist,new LayoutParams(-1,44));
            dist.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
                public void onProgressChanged(SeekBar b,int value,boolean user){if(user){entityRenderDist[et]=Math.max(value*5,10);distance.setText("Render distance  ·  "+entityRenderDist[et]+" m");}}
                public void onStartTrackingTouch(SeekBar b){}public void onStopTrackingTouch(SeekBar b){saveSettings();}
            });
            body.addView(text("Snap line origin",16,MUTED),new LayoutParams(-1,34));buildSnapOriginPicker();
            TextView link=text("Model colors  →",17,ACCENT);body.addView(link,new LayoutParams(-1,46));
            link.setOnClickListener(new OnClickListener(){public void onClick(View v){selectTab(6);}});
            buildVisualsColorSection();
        }
        void buildVisualsColorSection() {
            colorSlot=selectedEntityType;if(colorSlot>ET_NPC)colorSlot=ET_ENEMY;
            sectionIn("Visibility colors",null,1);
            LinearLayout states = row();
            for (int state = 0; state < 2; state++) {
                final boolean visible = state == 0;
                TextView option = text(visible ? "Visible" : "Occluded", 20,
                    colorVisible == visible ? INK : MUTED);
                option.setGravity(Gravity.CENTER);
                option.setBackground(shape(colorVisible == visible ? BG_ACTIVE : BG_CARD, 8));
                LayoutParams lp = new LayoutParams(0, 52, 1); lp.setMargins(0, 10, 6, 0);
                states.addView(option, lp);
                option.setOnClickListener(new OnClickListener() {
                    public void onClick(View v) { colorVisible = visible; selectTab(0); }
                });
            }
            body.addView(states);
            final TextView swatch = text("", 22, INK); swatch.setGravity(Gravity.CENTER);
            LayoutParams swatchLp = new LayoutParams(-1, 56); swatchLp.setMargins(0, 14, 0, 10);
            body.addView(swatch, swatchLp);
            final int editingSlot = colorSlot;
            final boolean editingVisible = colorVisible;
            final float[] hsv = new float[3]; Color.colorToHSV(gvc(editingSlot, editingVisible), hsv);
            updateSwatch(swatch);
            String[] components = {"Hue", "Saturation", "Brightness"};
            for (int i = 0; i < 3; i++) {
                final int component = i;
                body.addView(text(components[i], 17, MUTED), new LayoutParams(-1, 28));
                SeekBar slider = new SeekBar(getContext()); slider.setMax(i == 0 ? 360 : 100);
                slider.setProgress(Math.round(hsv[i] * (i == 0 ? 1 : 100)));
                styleSlider(slider);
                body.addView(slider, new LayoutParams(-1, 42));
                slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                    public void onProgressChanged(SeekBar bar, int value, boolean user) {
                        if (!user) return;
                        hsv[component] = component == 0 ? value : value / 100f;
                        setVisibilityColor(editingSlot, editingVisible, Color.HSVToColor(hsv)); updateSwatch(swatch);
                    }
                    public void onStartTrackingTouch(SeekBar bar) {}
                    public void onStopTrackingTouch(SeekBar bar) { saveSettings(); }
                });
            }
            sectionIn("Line weight",null,1);
            final TextView thickness=text(String.format(java.util.Locale.ROOT,"Stroke  ·  %.1f px",lineWidth),18,INK);body.addView(thickness,new LayoutParams(-1,38));
            SeekBar weight = new SeekBar(getContext()); weight.setMax(40);
            weight.setProgress(Math.round((lineWidth - 1) * 10));
            styleSlider(weight);
            body.addView(weight, new LayoutParams(-1, 42));
            weight.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                public void onProgressChanged(SeekBar bar, int value, boolean user) {
                    if(user){lineWidth=1+value/10f;thickness.setText(String.format(java.util.Locale.ROOT,"Stroke  ·  %.1f px",lineWidth));}
                }
                public void onStartTrackingTouch(SeekBar bar) {}
                public void onStopTrackingTouch(SeekBar bar) { saveSettings(); }
            });
        }
        void buildChamsTab() {
            sectionIn("Model coloring",null,0);toggle("Enable chams",-17,chamsEnabled);
            final TextView intensity=text("Brightness  ·  "+chamsBrightness+"%",18,INK);body.addView(intensity,new LayoutParams(-1,38));
            SeekBar brightness=new SeekBar(getContext());brightness.setMax(90);brightness.setProgress(chamsBrightness-10);styleSlider(brightness);body.addView(brightness,new LayoutParams(-1,44));
            brightness.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
                public void onProgressChanged(SeekBar b,int value,boolean user){if(user){chamsBrightness=value+10;intensity.setText("Brightness  ·  "+chamsBrightness+"%");}}
                public void onStartTrackingTouch(SeekBar b){}public void onStopTrackingTouch(SeekBar b){saveSettings();}
            });
            sectionIn("Visible surface",null,0);buildChamsColorPicker("Visible",true);
            sectionIn("Occluded surface",null,0);buildChamsColorPicker("Behind cover",false);
            sectionIn("Preview",null,1);body.addView(new EspPreview(getContext(),true),new LayoutParams(-1,400));
            sectionIn("Color presets","Choose a pair, then fine-tune the channels.",1);
            final int[][] presets={{0xFF00FF00,0xFFFF0000},{0xFF6DE5FF,0xFFFF6E9F},{0xFFA99AFF,0xFFFFC66D}};
            String[] names={"Classic","Ice / rose","Violet / gold"};
            for(int i=0;i<3;i++){final int index=i;TextView choice=text(names[i],18,INK);choice.setPadding(12,0,12,0);choice.setCompoundDrawablePadding(12);
                GradientDrawable dot=shape(presets[i][0],6);dot.setBounds(0,0,16,16);choice.setCompoundDrawables(dot,null,null,null);choice.setBackground(surface(0xFF202532,9));
                LayoutParams lp=new LayoutParams(-1,44);lp.setMargins(0,6,0,0);body.addView(choice,lp);
                choice.setOnClickListener(new OnClickListener(){public void onClick(View v){chamsVisibleColor=presets[index][0];chamsInvisibleColor=presets[index][1];saveSettings();selectTab(6);}});
            }
        }

        void colorSwatch(TextView swatch,int color,String value) {
            swatch.setText(value);swatch.setTextColor(INK);swatch.setGravity(Gravity.CENTER_VERTICAL);swatch.setPadding(12,0,12,0);
            swatch.setBackground(surface(0xFF222735,9));swatch.setSingleLine(true);swatch.setEllipsize(android.text.TextUtils.TruncateAt.END);
            GradientDrawable dot=shape(color,5);dot.setBounds(0,0,22,22);swatch.setCompoundDrawables(dot,null,null,null);swatch.setCompoundDrawablePadding(12);
        }
        void buildChamsColorPicker(String label,final boolean visible) {
            int color=visible?chamsVisibleColor:chamsInvisibleColor;
            final TextView swatch=text("",17,INK);swatch.setTypeface(Typeface.MONOSPACE);colorSwatch(swatch,color,String.format("#%06X",color&0xFFFFFF));
            body.addView(swatch,new LayoutParams(-1,42));
            final SeekBar[] sliders=new SeekBar[3];final TextView[] values=new TextView[3];
            String[] labels={"R","G","B"};int[] tints={0xFFEF8E9D,0xFF86DAB3,0xFF91B6FF};
            for(int i=0;i<3;i++) {
                LinearLayout line=row();line.setGravity(Gravity.CENTER_VERTICAL);TextView channel=text(labels[i],14,tints[i]);line.addView(channel,new LayoutParams(24,42));
                sliders[i]=new SeekBar(getContext());sliders[i].setMax(255);sliders[i].setProgress((color>>(16-8*i))&255);styleSlider(sliders[i]);
                sliders[i].setProgressTintList(ColorStateList.valueOf(tints[i]));line.addView(sliders[i],new LayoutParams(0,42,1));
                values[i]=text(Integer.toString(sliders[i].getProgress()),13,MUTED);values[i].setGravity(Gravity.RIGHT|Gravity.CENTER_VERTICAL);line.addView(values[i],new LayoutParams(28,42));
                body.addView(line,new LayoutParams(-1,42));
            }
            SeekBar.OnSeekBarChangeListener listener=new SeekBar.OnSeekBarChangeListener(){
                public void onProgressChanged(SeekBar bar,int n,boolean user){if(!user)return;
                    int color=Color.rgb(sliders[0].getProgress(),sliders[1].getProgress(),sliders[2].getProgress());
                    if(visible)chamsVisibleColor=color;else chamsInvisibleColor=color;
                    for(int i=0;i<3;i++)values[i].setText(Integer.toString(sliders[i].getProgress()));colorSwatch(swatch,color,String.format("#%06X",color&0xFFFFFF));
                }
                public void onStartTrackingTouch(SeekBar b){}public void onStopTrackingTouch(SeekBar b){saveSettings();}
            };
            for(SeekBar slider:sliders)slider.setOnSeekBarChangeListener(listener);
        }

        void buildSnapOriginPicker() {
            LinearLayout row = row();
            String[] labels = {"Bottom", "Top"};
            for (int i = 0; i < 2; i++) {
                final int val = i;
                TextView item = text(labels[i], 19, i == snapOrigin ? INK : MUTED);
                item.setGravity(Gravity.CENTER);
                item.setBackground(shape(i == snapOrigin ? BG_ACTIVE : BG_CARD, 8));
                LayoutParams lp = new LayoutParams(0, 52, 1); lp.setMargins(0, 0, 8, 0);
                row.addView(item, lp);
                item.setOnClickListener(new OnClickListener() {
                    public void onClick(View v) { snapOrigin = val; saveSettings(); selectTab(0); }
                });
            }
            body.addView(row, new LayoutParams(-1, 58));
        }

        void buildEntityTypePicker() {
            LinearLayout choices=row();choices.setTag("visual-actor-types");
            for(int i=0;i<3;i++) {
                final int type=i;TextView item=text(ET_NAMES[i],19,i==selectedEntityType?INK:MUTED);item.setGravity(Gravity.CENTER);
                item.setBackground(surface(i==selectedEntityType?BG_ACTIVE:0xFF202532,8));LayoutParams lp=new LayoutParams(0,50,1);lp.setMargins(0,0,6,0);choices.addView(item,lp);
                item.setOnClickListener(new OnClickListener(){public void onClick(View v){selectedEntityType=type;selectTab(0);}});
            }
            body.addView(choices,new LayoutParams(-1,56));
        }

        void etToggle(String name, final int flag, boolean enabled) {
            Switch control = new Switch(getContext()); control.setText(name); control.setTextColor(INK);
            control.setTextSize(TypedValue.COMPLEX_UNIT_PX, 25); control.setChecked(enabled);
            control.setPadding(12, 0, 14, 0);
            control.setShowText(false); control.setSplitTrack(false); control.setSwitchMinWidth(60);
            styleSwitch(control);
            body.addView(control, new LayoutParams(-1, 60));
            final int et = selectedEntityType;
            control.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
                public void onCheckedChanged(CompoundButton button, boolean checked) {
                    if (checked) entityFeatures[et] |= flag;
                    else entityFeatures[et] &= ~flag;
                    saveSettings();
                }
            });
        }

        /* ---- Colors tab ---- */
        void buildColorsTab() {
            if(colorSlot>ET_NPC)colorSlot=ET_ENEMY;
            section("Visibility colors", "Choose a player or NPC category. Bots use player colors.");
            LinearLayout targets=row(),second=row();targets.setTag("color-actor-types");
            for(int i=0;i<3;i++) {
                final int slot=i;TextView target=text(ET_NAMES[i],17,i==colorSlot?INK:MUTED);
                target.setGravity(Gravity.CENTER);target.setBackground(surface(i==colorSlot?BG_ACTIVE:0xFF202532,8));
                LayoutParams lp=new LayoutParams(0,52,1);lp.setMargins(0,0,6,5);(i<3?targets:second).addView(target,lp);
                target.setOnClickListener(new OnClickListener(){public void onClick(View v){colorSlot=slot;selectTab(1);}});
            }
            body.addView(targets,new LayoutParams(-1,54));
            if (colorSlot == ET_LOOT) { lootSettingsLink(); return; }

            LinearLayout states = row();
            for (int state = 0; state < 2; state++) {
                final boolean visible = state == 0;
                TextView option = text(visible ? "Visible" : "Occluded", 20,
                    colorVisible == visible ? INK : MUTED);
                option.setGravity(Gravity.CENTER);
                option.setBackground(shape(colorVisible == visible ? BG_ACTIVE : BG_CARD, 8));
                LayoutParams lp = new LayoutParams(0, 52, 1); lp.setMargins(0, 10, 6, 0);
                states.addView(option, lp);
                option.setOnClickListener(new OnClickListener() {
                    public void onClick(View v) { colorVisible = visible; selectTab(1); }
                });
            }
            body.addView(states);
            final TextView swatch = text("", 22, INK); swatch.setGravity(Gravity.CENTER);
            LayoutParams swatchLp = new LayoutParams(-1, 56); swatchLp.setMargins(0, 14, 0, 10);
            body.addView(swatch, swatchLp);
            final int editingSlot = colorSlot;
            final boolean editingVisible = colorVisible;
            final float[] hsv = new float[3]; Color.colorToHSV(gvc(editingSlot, editingVisible), hsv);
            updateSwatch(swatch);
            String[] components = {"Hue", "Saturation", "Brightness"};
            for (int i = 0; i < 3; i++) {
                final int component = i;
                body.addView(text(components[i], 17, MUTED), new LayoutParams(-1, 28));
                SeekBar slider = new SeekBar(getContext()); slider.setMax(i == 0 ? 360 : 100);
                slider.setProgress(Math.round(hsv[i] * (i == 0 ? 1 : 100)));
                styleSlider(slider);
                body.addView(slider, new LayoutParams(-1, 42));
                slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                    public void onProgressChanged(SeekBar bar, int value, boolean user) {
                        if (!user) return;
                        hsv[component] = component == 0 ? value : value / 100f;
                        setVisibilityColor(editingSlot, editingVisible, Color.HSVToColor(hsv)); updateSwatch(swatch);
                    }
                    public void onStartTrackingTouch(SeekBar bar) {}
                    public void onStopTrackingTouch(SeekBar bar) { saveSettings(); }
                });
            }
            section("Line weight",null);
            final TextView thickness=text(String.format(java.util.Locale.ROOT,"Stroke  ·  %.1f px",lineWidth),18,INK);body.addView(thickness,new LayoutParams(-1,38));
            SeekBar weight = new SeekBar(getContext()); weight.setMax(40);
            weight.setProgress(Math.round((lineWidth - 1) * 10));
            styleSlider(weight);
            body.addView(weight, new LayoutParams(-1, 42));
            weight.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                public void onProgressChanged(SeekBar bar, int value, boolean user) {
                    if(user){lineWidth=1+value/10f;thickness.setText(String.format(java.util.Locale.ROOT,"Stroke  ·  %.1f px",lineWidth));}
                }
                public void onStartTrackingTouch(SeekBar bar) {}
                public void onStopTrackingTouch(SeekBar bar) { saveSettings(); }
            });
            sectionIn("Preview",null,1);body.addView(new EspPreview(getContext(),false),new LayoutParams(-1,350));
        }

        void lootSettingsLink() {
            section("Loot rarity colors", "Loot uses the game's item colors. Configure filters in Loot.");
            TextView link = text("Open loot settings", 20, ACCENT);
            link.setPadding(12, 14, 12, 14); body.addView(link, new LayoutParams(-1, 56));
            link.setOnClickListener(new OnClickListener() { public void onClick(View v) { selectTab(5); } });
        }
        void buildLootTab() {
            sectionIn("Display",null,0);
            int old=selectedEntityType;selectedEntityType=ET_LOOT;
            etToggle("Show loot",F_ENABLED,(entityFeatures[ET_LOOT]&F_ENABLED)!=0);
            etToggle("Item names & stack counts",F_NAMES,(entityFeatures[ET_LOOT]&F_NAMES)!=0);
            etToggle("Distance labels",F_DISTANCE,(entityFeatures[ET_LOOT]&F_DISTANCE)!=0);
            etToggle("Item markers",F_BOXES,(entityFeatures[ET_LOOT]&F_BOXES)!=0);selectedEntityType=old;
            toggle("Show reference value per item",-15,lootShowValue);
            toggle("Containers, stashes & death boxes",-16,lootShowContainers);
            sectionIn("Rarity filters",null,0);
            LinearLayout presets=row();String[] names={"All","Rare+","None"};final int[] masks={255,240,0};
            for(int i=0;i<3;i++){final int mask=masks[i];LayoutParams lp=new LayoutParams(0,48,1);lp.setMargins(0,0,8,0);presets.addView(action(names[i],new Runnable(){public void run(){lootRarityMask=mask;saveSettings();selectTab(5);}}),lp);}body.addView(presets,new LayoutParams(-1,56));
            final TextView count=text(Integer.bitCount(lootRarityMask&255)+" of 8 rarities selected",17,MUTED);body.addView(count,new LayoutParams(-1,40));
            for(int q=0;q<LOOT_RARITIES.length;q++){
                final int quality=q;Switch control=new Switch(getContext());control.setText(LOOT_RARITIES[q]);control.setTag(Integer.valueOf(q));control.setTextColor(lootGameColors[q]);control.setTextSize(TypedValue.COMPLEX_UNIT_PX,25);
                control.setChecked((lootRarityMask&(1<<q))!=0);control.setPadding(12,0,14,0);control.setShowText(false);control.setSplitTrack(false);styleSwitch(control);body.addView(control,new LayoutParams(-1,60));
                control.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener(){public void onCheckedChanged(CompoundButton b,boolean checked){if(checked)lootRarityMask|=1<<quality;else lootRarityMask&=~(1<<quality);count.setText(Integer.bitCount(lootRarityMask&255)+" of 8 rarities selected");saveSettings();}});
            }
            sectionIn("Preview",null,1);body.addView(new LootPreview(getContext()),new LayoutParams(-1,330));
            sectionIn("Range & value",null,1);
            final TextView minimum=text("Minimum value / item  ·  "+lootMinValue,20,INK);body.addView(minimum,new LayoutParams(-1,46));
            SeekBar value=new SeekBar(getContext());value.setMax(1000);value.setProgress(lootMinValue/1000);styleSlider(value);body.addView(value,new LayoutParams(-1,50));
            value.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onProgressChanged(SeekBar b,int n,boolean user){if(user){lootMinValue=n*1000;minimum.setText("Minimum value / item  ·  "+lootMinValue);}}public void onStartTrackingTouch(SeekBar b){}public void onStopTrackingTouch(SeekBar b){saveSettings();}});
            final TextView distance=text("Render distance  ·  "+entityRenderDist[ET_LOOT]+" m",20,INK);body.addView(distance,new LayoutParams(-1,46));
            SeekBar range=new SeekBar(getContext());range.setMax(100);range.setProgress(entityRenderDist[ET_LOOT]/10);styleSlider(range);body.addView(range,new LayoutParams(-1,50));
            range.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onProgressChanged(SeekBar b,int n,boolean user){if(user){entityRenderDist[ET_LOOT]=Math.max(10,n*10);distance.setText("Render distance  ·  "+entityRenderDist[ET_LOOT]+" m");}}public void onStartTrackingTouch(SeekBar b){}public void onStopTrackingTouch(SeekBar b){saveSettings();}});
            sectionIn("Value source","Reference values are per item. Unopened container contents are not estimated.",1);
        }
        class LootPreview extends View {
            final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
            LootPreview(Context context){super(context);setContentDescription("Illustrative loot preview");}
            void label(Canvas c,String text,float x,float y,int color,int size){paint.setStyle(Paint.Style.FILL);paint.setColor(color);paint.setTextSize(size);paint.setTypeface(Typeface.create("sans-serif-medium",Typeface.NORMAL));paint.setTextAlign(Paint.Align.CENTER);c.drawText(text,x,y,paint);}
            @Override protected void onDraw(Canvas c){
                float w=getWidth(),h=getHeight();paint.setColor(0xFF121620);paint.setStyle(Paint.Style.FILL);c.drawRoundRect(new RectF(0,0,w,h),12,12,paint);
                paint.setColor(0xFF222A38);paint.setStrokeWidth(1);for(int y=56;y<h-26;y+=38)c.drawLine(14,y,w-14,y,paint);
                int features=entityFeatures[ET_LOOT],shown=0;boolean enabled=espEnabled&&(features&F_ENABLED)!=0;
                String[] names={"Optic","Medkit","Ammo ×30"};int[] qualities={5,4,2},values={48000,18000,600},distances={14,28,36};
                label(c,"LOOT  /  LIVE SETTINGS",w/2,28,0xFF96A2B8,15);
                for(int i=0;i<3;i++){
                    if(!enabled||(lootRarityMask&(1<<qualities[i]))==0||values[i]<lootMinValue||distances[i]>entityRenderDist[ET_LOOT])continue;shown++;
                    float x=w*(.18f+.32f*i),y=122;int color=lootGameColors[qualities[i]];
                    paint.setColor(color);paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(2);
                    if((features&F_BOXES)!=0)c.drawRoundRect(new RectF(x-23,y-23,x+23,y+23),6,6,paint);
                    paint.setColor(0xFF8292AA);if(i==0){c.drawCircle(x,y,12,paint);c.drawLine(x-17,y,x+17,y,paint);}else if(i==1){c.drawLine(x-10,y,x+10,y,paint);c.drawLine(x,y-10,x,y+10,paint);}else{c.drawRoundRect(new RectF(x-11,y-13,x-3,y+13),2,2,paint);c.drawRoundRect(new RectF(x+3,y-13,x+11,y+13),2,2,paint);}
                    if((features&F_NAMES)!=0){label(c,names[i],x,y+48,color,18);if(lootShowValue)label(c,String.format(java.util.Locale.ROOT,"%,d",values[i]),x,y+71,0xFFB4BDCC,16);}
                    if((features&F_DISTANCE)!=0)label(c,distances[i]+" m",x,y+94,0xFF7E8BA3,15);
                }
                if(enabled&&lootShowContainers){label(c,"▣  Container",w/2,h-59,0xFF9AA7BC,17);}
                if(shown==0)label(c,"No sample items match your filters",w/2,h/2,0xFF69778F,17);
                label(c,"ILLUSTRATIVE ITEMS  ·  FILTERS APPLY",w/2,h-15,0xFF58677F,12);
            }
        }

        /* ---- Aim tab ---- */
        void buildAimTab() {
            sectionIn("Aim area",null,0);
            toggle("360° targeting",-12,aim360);
            final TextView radius=text(aim360?"FOV radius  ·  Disabled in 360° mode":"FOV radius  ·  "+aimFov+"°",20,INK);body.addView(radius,new LayoutParams(-1,44));
            SeekBar fov=new SeekBar(getContext());fov.setContentDescription("Aim FOV radius");fov.setMax(40);fov.setProgress(aimFov-5);fov.setEnabled(!aim360);fov.setAlpha(aim360?.3f:1f);styleSlider(fov);body.addView(fov,new LayoutParams(-1,52));
            fov.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){public void onProgressChanged(SeekBar b,int n,boolean user){if(user){aimFov=n+5;radius.setText("FOV radius  ·  "+aimFov+"°");}}public void onStartTrackingTouch(SeekBar b){}public void onStopTrackingTouch(SeekBar b){saveSettings();}});

            section("Aim assist", "Locks onto a live, visible target inside the circle. Disable Aim to release.");
            toggle("Enable aim", -2, aimEnabled);
            toggle("Show lock button", -11, showLockBtn);

            section("Trigger bot", "Holds fire while a visible, living enemy is under the crosshair. Works with Aim off.");
            toggle("Auto shoot", -3, autoShoot);
            toggle("Auto zoom before shooting", -10, autoZoom);

            section("Target types", null);
            aimTargetToggle("Enemy players", AIM_ENEMIES);
            aimTargetToggle("Teammates", AIM_TEAMMATES);
            aimTargetToggle("NPCs", AIM_BOTS);

            section("Type preference", "Which type to aim first when multiple targets are in range");
            buildTypePrefPicker();

            section("Aim point", null);
            buildBonePicker();

            section("Sort priority", null);
            buildPriorityPicker();

            section("Max distance", aimMaxDist == 0 ? "Unlimited" : aimMaxDist + " m");
            SeekBar distSlider = new SeekBar(getContext()); distSlider.setMax(500); distSlider.setProgress(aimMaxDist);
            styleSlider(distSlider);
            body.addView(distSlider, new LayoutParams(-1, 42));
            distSlider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                public void onProgressChanged(SeekBar bar, int value, boolean user) {
                    if (user) aimMaxDist = value;
                }
                public void onStartTrackingTouch(SeekBar bar) {}
                public void onStopTrackingTouch(SeekBar bar) { saveSettings(); selectTab(2); }
            });

        }

        void aimTargetToggle(String name, final int flag) {
            Switch control = new Switch(getContext()); control.setText(name); control.setTextColor(INK);
            control.setTextSize(TypedValue.COMPLEX_UNIT_PX, 25);
            control.setChecked((aimTargetMask & flag) != 0);
            control.setPadding(12, 0, 14, 0);
            control.setShowText(false); control.setSplitTrack(false); control.setSwitchMinWidth(60);
            styleSwitch(control);
            body.addView(control, new LayoutParams(-1, 60));
            control.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
                public void onCheckedChanged(CompoundButton button, boolean checked) {
                    if (checked) aimTargetMask |= flag;
                    else aimTargetMask &= ~flag;
                    saveSettings();
                }
            });
        }

        void buildBonePicker() {
            LinearLayout row = row();
            String[] labels = {"Head", "Chest", "Body"};
            for (int i = 0; i < 3; i++) {
                final int bone = i;
                TextView item = text(labels[i], 19, i == aimBone ? INK : MUTED);
                item.setGravity(Gravity.CENTER);
                item.setBackground(shape(i == aimBone ? BG_ACTIVE : BG_CARD, 8));
                LayoutParams lp = new LayoutParams(0, 52, 1); lp.setMargins(0, 0, 8, 0);
                row.addView(item, lp);
                item.setOnClickListener(new OnClickListener() {
                    public void onClick(View v) { aimBone = bone; saveSettings(); selectTab(2); }
                });
            }
            body.addView(row, new LayoutParams(-1, 58));
        }

        void buildPriorityPicker() {
            String[] labels = {"Crosshair", "Distance", "Low HP", "Facing me"};
            LinearLayout row = row();
            for (int i = 0; i < 4; i++) {
                final int prio = i;
                TextView item = text(labels[i], 17, i == aimPriority ? INK : MUTED);
                item.setGravity(Gravity.CENTER);
                item.setBackground(shape(i == aimPriority ? BG_ACTIVE : BG_CARD, 8));
                LayoutParams lp = new LayoutParams(0, 52, 1); lp.setMargins(0, 0, 8, 0);
                row.addView(item, lp);
                item.setOnClickListener(new OnClickListener() {
                    public void onClick(View v) { aimPriority = prio; saveSettings(); selectTab(2); }
                });
            }
            body.addView(row, new LayoutParams(-1, 58));
        }

        void buildTypePrefPicker() {
            String[] labels = {"None", "Enemy", "Teammate", "NPC"};
            LinearLayout row = row();
            for (int i = 0; i < 4; i++) {
                final int tp = i;
                TextView item = text(labels[i], 17, i == aimTypePref ? INK : MUTED);
                item.setGravity(Gravity.CENTER);
                item.setBackground(shape(i == aimTypePref ? BG_ACTIVE : BG_CARD, 8));
                LayoutParams lp = new LayoutParams(0, 52, 1); lp.setMargins(0, 0, 8, 0);
                row.addView(item, lp);
                item.setOnClickListener(new OnClickListener() {
                    public void onClick(View v) { aimTypePref = tp; saveSettings(); selectTab(2); }
                });
            }
            body.addView(row, new LayoutParams(-1, 58));
        }

        /* ---- Misc tab ---- */
        void buildMiscTab() {
            sectionIn("Weapon",null,0);toggle("No recoil",-8,noRecoil);toggle("No spread",-9,noSpread);toggle("No Weapon Sway",-18,noWeaponSway);
            sectionIn("Perspective",null,0);toggle("Force third person",-19,forceTPP);
            sectionIn("Movement",null,1);toggle("Infinite stamina",-14,infiniteStamina);
            sectionIn("Camera",null,1);
            final TextView value=text("Field of view  ·  "+(cameraFov==0?"Default":cameraFov+"°"),18,INK);body.addView(value,new LayoutParams(-1,38));
            SeekBar slider=new SeekBar(getContext());slider.setMax(160);slider.setProgress(cameraFov==0?80:cameraFov-10);styleSlider(slider);body.addView(slider,new LayoutParams(-1,46));
            slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
                public void onProgressChanged(SeekBar b,int n,boolean user){if(user){cameraFov=n+10;value.setText("Field of view  ·  "+cameraFov+"°");}}
                public void onStartTrackingTouch(SeekBar b){}public void onStopTrackingTouch(SeekBar b){saveSettings();}
            });
            TextView reset=text("Reset to game default",16,MUTED);reset.setGravity(Gravity.CENTER);reset.setBackground(surface(0xFF222735,9));
            LayoutParams lp=new LayoutParams(-1,42);lp.setMargins(0,8,0,0);body.addView(reset,lp);
            reset.setOnClickListener(new OnClickListener(){public void onClick(View v){cameraFov=0;saveSettings();selectTab(3);}});
        }

        /* ---- Status tab ---- */
        TextView entTable;
        void buildStatusTab() {
            sectionIn("Live diagnostics", "A frame expires automatically when updates stop.",0);
            status = text("", 19, MUTED); status.setTypeface(Typeface.MONOSPACE);
            status.setGravity(Gravity.TOP); status.setPadding(12, 14, 12, 14);
            status.setBackground(shape(0xFF0B121D, 12));
            body.addView(status, new LayoutParams(-1, -2)); refreshStatus();
            sectionIn("Entities in lobby",null,0);
            android.widget.HorizontalScrollView hscroll = new android.widget.HorizontalScrollView(getContext());
            hscroll.setHorizontalScrollBarEnabled(true);hscroll.setOverScrollMode(OVER_SCROLL_NEVER);
            entTable = text("Waiting for data...", 14, MUTED); entTable.setTypeface(Typeface.MONOSPACE);
            entTable.setGravity(Gravity.TOP); entTable.setPadding(12, 10, 12, 10);
            entTable.setBackground(shape(0xFF0B121D, 12));
            entTable.setLineSpacing(3,1);
            hscroll.addView(entTable, new FrameLayout.LayoutParams(-2, -2));
            body.addView(hscroll, new LayoutParams(-1, -2)); refreshEntTable();
        }

        /* ---- Common toggle ---- */
        void toggle(String name, final int key, boolean enabled) {
            Switch control = new Switch(getContext()); control.setText(name); control.setTextColor(INK);
            control.setTextSize(TypedValue.COMPLEX_UNIT_PX, 26); control.setChecked(enabled);
            control.setPadding(12, 0, 14, 0);
            control.setShowText(false); control.setSplitTrack(false); control.setSwitchMinWidth(66);
            styleSwitch(control);
            body.addView(control, new LayoutParams(-1, 62));
            control.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
                public void onCheckedChanged(CompoundButton button, boolean checked) {
                    switch (key) {
                        case -1: espEnabled = checked; break;
                        case -2: aimEnabled = checked; updateLockBtnVisibility(); break;
                        case -3: autoShoot = checked; break;
                        case -10: autoZoom = checked; break;
                        case -11: showLockBtn = checked; updateLockBtnVisibility(); break;
                        case -4: rayDebug = checked; break;
                        case -5: rayViews = checked; break;
                        case -6: rayLos = checked; break;
                        case -7: rayBounds = checked; break;
                        case -8: noRecoil = checked; break;
                        case -9: noSpread = checked; break;
                        case -18: noWeaponSway = checked; break;
                        case -12: aim360 = checked; saveSettings(); selectTab(2); return;
                        case -14: infiniteStamina = checked; break;
                        case -15: lootShowValue = checked; break;
                        case -17: chamsEnabled = checked; break;
                        case -16: lootShowContainers = checked; break;
                        case -19: forceTPP = checked; break;
                    }
                    saveSettings(); if (inst != null) inst.invalidate();
                }
            });
        }

        void styleSwitch(Switch control) {
            GradientDrawable thumb = shape(0xFFE7EDF5, 14); thumb.setSize(28, 28);
            control.setThumbDrawable(thumb);
            GradientDrawable track = shape(0xFF2B3B50, 16); track.setSize(64, 30);
            control.setTrackDrawable(track);
            control.setTrackTintList(new android.content.res.ColorStateList(
                new int[][] {new int[] {android.R.attr.state_checked}, new int[] {}},
                new int[] {0xFF8370E4, 0xFF343B4C}));
            control.setBackground(new RippleDrawable(ColorStateList.valueOf(0x159C8CFF),null,shape(Color.WHITE,8)));
            control.setThumbTintList(new android.content.res.ColorStateList(
                new int[][] {new int[] {android.R.attr.state_checked}, new int[] {}},
                new int[] {0xFFF2EFFF, 0xFF929AAF}));
        }

        void styleSlider(SeekBar slider) {
            GradientDrawable track = shape(0xFF33394B, 4); track.setSize(64, 5);
            GradientDrawable progress = shape(ACCENT, 4); progress.setSize(64, 5);
            LayerDrawable layers = new LayerDrawable(new android.graphics.drawable.Drawable[] {
                track, new ClipDrawable(progress, Gravity.LEFT, ClipDrawable.HORIZONTAL)});
            layers.setId(0, android.R.id.background); layers.setId(1, android.R.id.progress);
            layers.setLayerGravity(0,Gravity.CENTER_VERTICAL);layers.setLayerHeight(0,5);
            layers.setLayerGravity(1,Gravity.CENTER_VERTICAL);layers.setLayerHeight(1,5);
            slider.setProgressDrawable(layers);
            GradientDrawable thumb = shape(0xFFE7EDF5, 12); thumb.setSize(24, 24);
            slider.setThumb(thumb); slider.setThumbOffset(12); slider.setPadding(12, 0, 12, 0);
        }

        void updateSwatch(TextView swatch) {
            int color=gvc(colorSlot,colorVisible);
            colorSwatch(swatch,color,String.format("%s  ·  #%06X",colorVisible?"Visible":"Occluded",color&0xFFFFFF));
        }

        void refreshStatus() {
            if (status == null) return;
            long age = frameAt == 0 ? -1 : SystemClock.uptimeMillis() - frameAt;
            status.setText("Viewport    " + sw + " × " + sh +
                "\nFrame age   " + (age < 0 ? "waiting" : age + " ms") +
                "\nEntities    " + diagEntities + "\nWorld       " + diagGWorld +
                "\nName pool   " + diagFName + "\nReader      " + diagStatus +
                (diagExtra.length() == 0 ? "" : "\n" + diagExtra));
            refreshEntTable();
        }
        void refreshEntTable() {
            if (entTable == null) return;
            String data = diagEntList;
            entTable.setText(data.isEmpty() ? "No entities" : data);
        }
    }
}
