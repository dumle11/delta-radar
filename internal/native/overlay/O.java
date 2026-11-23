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
        final Runnable capture = new Runnable() {
            public void run() {
                if (!isAttachedToWindow()) return;
                int w=getWidth(), h=getHeight();
                if (w>0 && h>0) {
                    if (pixels==null || pixels.getWidth()!=w || pixels.getHeight()!=h) {
                        if(pixels!=null) pixels.recycle();
                        pixels=Bitmap.createBitmap(w,h,Bitmap.Config.ARGB_8888);
                        canvas=new Canvas(pixels);
                    }
                    canvas.drawColor(Color.TRANSPARENT,PorterDuff.Mode.CLEAR);
                    paintChildren(canvas);
                    submitPixels(pixels,getDisplay()==null?0:getDisplay().getRotation());
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
                postDelayed(this,33);
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
        void addView(View view,WindowManager.LayoutParams lp) { host.addView(view,lp); }
        void removeView(View view) { host.removeView(view); }
        void updateViewLayout(View view,WindowManager.LayoutParams lp) {
            lp.x=clampPosition(lp.x,host.getWidth(),view.getWidth());
            lp.y=clampPosition(lp.y,host.getHeight(),view.getHeight());
            view.setLayoutParams(lp); host.requestLayout();
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

    // Misc settings
    static volatile boolean noRecoil = false;
    static volatile boolean noSpread = false;
    static volatile boolean infiniteStamina = false;
    static volatile int cameraFov = 0; // 0 = game default, 10-170 = override

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

    O(Context c) { super(c); }

    static synchronized void z(final Activity a) {
        if (initDone) return;
        initDone = true;
        uiHandler = new Handler(Looper.getMainLooper());
        a.runOnUiThread(new Runnable() {
            public void run() {
                try {
                    prefs = a.getSharedPreferences("overlay_v2", Context.MODE_PRIVATE);
                    espEnabled = prefs.getBoolean("esp", true);
                    rayDebug = prefs.getBoolean("rayDebug", false);
                    rayViews = prefs.getBoolean("rayViews", true);
                    rayLos = prefs.getBoolean("rayLos", true);
                    rayBounds = prefs.getBoolean("rayBounds", true);
                    lineWidth = prefs.getFloat("width", 2.5f);
                    menuX = prefs.getInt("menuX", -1); menuY = prefs.getInt("menuY", -1);
                    snapOrigin = prefs.getInt("snapOrigin", 0);
                    for (int i = 0; i < ET_COUNT; i++) {
                        entityColors[i] = prefs.getInt(entityKey("color", i), defaultColors[i]);
                        visibleColors[i] = prefs.getInt(entityKey("visibleColor", i), VISIBLE_COLOR);
                        occludedColors[i] = prefs.getInt(entityKey("occludedColor", i), OCCLUDED_COLOR);
                        entityFeatures[i] = prefs.getInt(entityKey("feat", i), defaultFeatures(i));
                        entityRenderDist[i] = prefs.getInt(entityKey("rdist", i), 500);
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
                    aimPriority = prefs.getInt("aimPriority", 0);
                    noRecoil = prefs.getBoolean("noRecoil", false);
                    noSpread = prefs.getBoolean("noSpread", false);
                    infiniteStamina = prefs.getBoolean("infiniteStamina", false);
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

    /* Per-entity-type queries (called from C) */
    static boolean ge() { return espEnabled; }
    static int grd() { return rayDebug ? 1 | (rayViews ? 2 : 0) | (rayLos ? 4 : 0) | (rayBounds ? 8 : 0) : 0; }
    static int gf(int type) { return (type >= 0 && type < ET_COUNT) ? entityFeatures[type] : 0; }
    static int gr(int type) { return (type >= 0 && type < ET_COUNT) ? entityRenderDist[type] : 500; }
    static int gso() { return snapOrigin; }
    static int gc(int slot) {
        return (slot >= 0 && slot < ET_COUNT) ? entityColors[slot] : 0xFFE5534B;
    }
    static int gvc(int slot, boolean visible) {
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
    static boolean gal() { return aimLocked; }
    static boolean ga3() { return aim360; }
    static int gap() { return aimPriority; }

    /* Misc queries (called from C) */
    static boolean gnr() { return noRecoil; }
    static boolean gns() { return noSpread; }
    static boolean gis() { return infiniteStamina; }
    static int gcf() { return cameraFov; }

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
                for (int i=0;i<menu.body.getChildCount();i++) {
                    View child=menu.body.getChildAt(i);
                    if (child instanceof Switch && Integer.valueOf(quality).equals(child.getTag()))
                        ((Switch)child).setTextColor(color);
                }
            }
        });
    }

    static void setColor(int slot, int color) {
        if (slot >= 0 && slot < ET_COUNT) entityColors[slot] = color;
    }
    static void saveSettings() {
        if (prefs == null) return;
        SharedPreferences.Editor edit = prefs.edit().putBoolean("esp", espEnabled)
            .putFloat("width", lineWidth).putInt("menuX", menuX).putInt("menuY", menuY)
            .putInt("snapOrigin", snapOrigin)
            .putBoolean("aim", aimEnabled).putInt("aimFov", aimFov)
            .putInt("aimStr", aimStrength).putInt("aimBone", aimBone)
            .putBoolean("autoShoot", autoShoot).putBoolean("autoZoom", autoZoom).putBoolean("showLockBtn", showLockBtn).putBoolean("aim360", aim360).putInt("aimMask", aimTargetMask).putInt("aimPriority", aimPriority)
            .putBoolean("rayDebug", rayDebug).putBoolean("rayViews", rayViews)
            .putBoolean("rayLos", rayLos).putBoolean("rayBounds", rayBounds)
            .putBoolean("noRecoil", noRecoil).putBoolean("noSpread", noSpread).putBoolean("infiniteStamina", infiniteStamina)
            .putInt("cameraFov", cameraFov)
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

    static volatile boolean menuShown = false;

    static void toggleMenu() {
        if (wm == null || menu == null || bubble == null) return;
        if (menuShown) {
            try { wm.removeView(menu); } catch (Exception ex) {}
            menuShown = false;
        } else {
            WindowManager.LayoutParams mlp = new WindowManager.LayoutParams(
                Math.min(760, Math.max(280, viewWidth - 120)),
                Math.min(800, Math.max(280, viewHeight - 60)),
                WindowManager.LayoutParams.TYPE_APPLICATION,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                PixelFormat.TRANSLUCENT);
            WindowManager.LayoutParams blp = (WindowManager.LayoutParams) bubble.getLayoutParams();
            mlp.gravity = Gravity.TOP | Gravity.LEFT;
            mlp.x = clampPosition(menuX < 0 ? blp.x + 100 : menuX, viewWidth, mlp.width);
            mlp.y = clampPosition(menuY < 0 ? blp.y - 100 : menuY, viewHeight, mlp.height);
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
            cp.setColor(aimLocked ? 0xCC00C853 : 0xCC555555);
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
        static final int INK = 0xFFE7EDF5, MUTED = 0xFF94A3B8, ACCENT = 0xFF5EE2B0;
        static final int BG_DARK = 0xF5101825, BG_CARD = 0xFF1D2939, BG_ACTIVE = 0xFF30455B;
        LinearLayout body, nav;
        TextView status, summary, dragHandle;
        int selectedTab, selectedEntityType, colorSlot;
        boolean colorVisible = true;
        final String[] colorNames = ET_NAMES;

        MenuPanel(Context context) {
            super(context);
            setOrientation(VERTICAL); setPadding(24, 20, 24, 20);
            setDescendantFocusability(FOCUS_BLOCK_DESCENDANTS);
            setBackground(shape(BG_DARK, 24));
            setElevation(16);
            LinearLayout header = row();
            TextView title = text("DELTA  /  VISUAL STUDIO", 25, INK);
            dragHandle = title;
            title.setContentDescription("Drag menu");
            installDrag(title);
            title.setTypeface(Typeface.DEFAULT_BOLD);
            header.addView(title, new LayoutParams(0, 54, 1));
            TextView close = text("×", 32, MUTED); close.setGravity(Gravity.CENTER);
            header.addView(close, new LayoutParams(48, 54));
            close.setOnClickListener(new OnClickListener() { public void onClick(View v) { toggleMenu(); } });
            addView(header);
            summary = text("Drag the header to move", 18, MUTED);
            addView(summary, new LayoutParams(-1, 36));
            nav = row(); nav.setPadding(0, 8, 0, 14);
            addView(nav, new LayoutParams(-1, 68));
            ScrollView scroll = new ScrollView(context); scroll.setFillViewport(true);
            body = new LinearLayout(context); body.setOrientation(VERTICAL);
            scroll.addView(body, new ScrollView.LayoutParams(-1, -2));
            addView(scroll, new LayoutParams(-1, 0, 1));
            selectTab(0);
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
            label.setTextSize(TypedValue.COMPLEX_UNIT_PX, size); label.setGravity(Gravity.CENTER_VERTICAL);
            return label;
        }
        LinearLayout row() {
            LinearLayout line = new LinearLayout(getContext()); line.setOrientation(HORIZONTAL); return line;
        }
        void section(String title, String subtitle) {
            TextView heading = text(title, 22, INK); heading.setTypeface(Typeface.DEFAULT_BOLD);
            LayoutParams hlp = new LayoutParams(-1, 40); hlp.setMargins(0, 10, 0, 0);
            body.addView(heading, hlp);
            if (subtitle != null) body.addView(text(subtitle, 17, MUTED), new LayoutParams(-1, 32));
        }
        void selectTab(int tab) {
            selectedTab = tab; nav.removeAllViews(); body.removeAllViews(); status = null;
            String[] titles = {"Visuals", "Colors", "Aim", "Misc", "Status", "Loot"};
            for (int i = 0; i < titles.length; i++) {
                final int target = i;
                TextView item = text(titles[i], 20, i == tab ? 0xFF0C201A : MUTED);
                item.setGravity(Gravity.CENTER); item.setTypeface(Typeface.DEFAULT_BOLD);
                item.setBackground(shape(i == tab ? ACCENT : BG_CARD, 12));
                LayoutParams lp = new LayoutParams(0, -1, 1); lp.setMargins(0, 0, 6, 0);
                nav.addView(item, lp);
                item.setOnClickListener(new OnClickListener() { public void onClick(View v) { selectTab(target); } });
            }
            switch (tab) {
                case 0: buildVisualsTab(); break;
                case 1: buildColorsTab(); break;
                case 2: buildAimTab(); break;
                case 3: buildMiscTab(); break;
                case 4: buildStatusTab(); break;
                case 5: buildLootTab(); break;
            }
        }

        /* ---- Visuals tab ---- */
        void buildVisualsTab() {
            toggle("Enable visuals", -1, espEnabled);
            section("Snap line origin", null);
            buildSnapOriginPicker();
            section("Per-entity settings", selectedEntityType == ET_NPC ? "World NPCs and training targets." :
                selectedEntityType == ET_BOT_PLAYER ? "Simulated players from either team." : "Configure what each entity type shows.");
            buildEntityTypePicker();
            if (selectedEntityType == ET_LOOT) {
                lootSettingsLink(); return;
            }
            if (selectedEntityType >= 0 && selectedEntityType < ET_COUNT) {
                int feat = entityFeatures[selectedEntityType];
                etToggle("Enabled", F_ENABLED, (feat & F_ENABLED) != 0);
                etToggle("Boxes", F_BOXES, (feat & F_BOXES) != 0);
                etToggle("Skeleton", F_SKELETON, (feat & F_SKELETON) != 0);
                etToggle("Names & types", F_NAMES, (feat & F_NAMES) != 0);
                etToggle("Health bars", F_HEALTH, (feat & F_HEALTH) != 0);
                etToggle("Distance", F_DISTANCE, (feat & F_DISTANCE) != 0);
                etToggle("Snap lines", F_SNAPLINES, (feat & F_SNAPLINES) != 0);
                section("Render distance", entityRenderDist[selectedEntityType] + "m");
                SeekBar dist = new SeekBar(getContext()); dist.setMax(200);
                dist.setProgress(entityRenderDist[selectedEntityType] / 5);
                styleSlider(dist);
                body.addView(dist, new LayoutParams(-1, 42));
                final int et = selectedEntityType;
                dist.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                    public void onProgressChanged(SeekBar bar, int value, boolean user) {
                        if (!user) return;
                        entityRenderDist[et] = Math.max(value * 5, 10);
                        selectTab(0); // refresh label
                    }
                    public void onStartTrackingTouch(SeekBar bar) {}
                    public void onStopTrackingTouch(SeekBar bar) { saveSettings(); }
                });
            }
            section("Chams", chamsEnabled ? "Colored models through walls" : null);
            toggle("Enable chams", -17, chamsEnabled);
            if (chamsEnabled) {
                final TextView intensity = text("Brightness: " + chamsBrightness + "%", 18, INK);
                intensity.setPadding(12, 8, 12, 0);
                body.addView(intensity, new LayoutParams(-1, -2));
                SeekBar brightness = new SeekBar(getContext());
                brightness.setMax(90); brightness.setProgress(chamsBrightness - 10);
                styleSlider(brightness); body.addView(brightness, new LayoutParams(-1, 42));
                brightness.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                    public void onProgressChanged(SeekBar bar, int value, boolean user) {
                        if (!user) return;
                        chamsBrightness = value + 10;
                        intensity.setText("Brightness: " + chamsBrightness + "%");
                    }
                    public void onStartTrackingTouch(SeekBar bar) {}
                    public void onStopTrackingTouch(SeekBar bar) { saveSettings(); }
                });
                buildChamsColorPicker("Visible", true);
                buildChamsColorPicker("Behind walls", false);
            }
        }

        void buildChamsColorPicker(String label, final boolean visible) {
            final int color = visible ? chamsVisibleColor : chamsInvisibleColor;
            TextView lbl = text(label, 18, INK);
            lbl.setPadding(12, 8, 12, 0); body.addView(lbl, new LayoutParams(-1, -2));
            final TextView swatch = text(String.format("#%06X", color & 0xFFFFFF), 17, INK);
            swatch.setBackground(shape(color, 8));
            swatch.setTextColor(Color.luminance(color) > 0.4f ? 0xFF101825 : Color.WHITE);
            swatch.setPadding(12, 6, 12, 6); swatch.setGravity(Gravity.CENTER);
            body.addView(swatch, new LayoutParams(-1, 38));
            final SeekBar sr = new SeekBar(getContext()); sr.setMax(255); sr.setProgress((color >> 16) & 0xFF);
            final SeekBar sg = new SeekBar(getContext()); sg.setMax(255); sg.setProgress((color >> 8) & 0xFF);
            final SeekBar sb = new SeekBar(getContext()); sb.setMax(255); sb.setProgress(color & 0xFF);
            for (SeekBar s : new SeekBar[]{sr, sg, sb}) { styleSlider(s); body.addView(s, new LayoutParams(-1, 34)); }
            SeekBar.OnSeekBarChangeListener cl = new SeekBar.OnSeekBarChangeListener() {
                public void onProgressChanged(SeekBar bar, int value, boolean user) {
                    if (!user) return;
                    int c = 0xFF000000 | (sr.getProgress() << 16) | (sg.getProgress() << 8) | sb.getProgress();
                    if (visible) chamsVisibleColor = c; else chamsInvisibleColor = c;
                    swatch.setBackground(shape(c, 8));
                    swatch.setText(String.format("#%06X", c & 0xFFFFFF));
                    swatch.setTextColor(Color.luminance(c) > 0.4f ? 0xFF101825 : Color.WHITE);
                }
                public void onStartTrackingTouch(SeekBar bar) {}
                public void onStopTrackingTouch(SeekBar bar) { saveSettings(); }
            };
            sr.setOnSeekBarChangeListener(cl); sg.setOnSeekBarChangeListener(cl); sb.setOnSeekBarChangeListener(cl);
        }

        void buildSnapOriginPicker() {
            LinearLayout row = row();
            String[] labels = {"Bottom", "Top"};
            for (int i = 0; i < 2; i++) {
                final int val = i;
                TextView item = text(labels[i], 19, i == snapOrigin ? INK : MUTED);
                item.setGravity(Gravity.CENTER);
                item.setBackground(shape(i == snapOrigin ? BG_ACTIVE : BG_CARD, 8));
                LayoutParams lp = new LayoutParams(0, 44, 1); lp.setMargins(0, 0, 8, 0);
                row.addView(item, lp);
                item.setOnClickListener(new OnClickListener() {
                    public void onClick(View v) { snapOrigin = val; saveSettings(); selectTab(0); }
                });
            }
            body.addView(row, new LayoutParams(-1, 48));
        }

        void buildEntityTypePicker() {
            LinearLayout row1 = row(), row2 = row();
            for (int i = 0; i < ET_COUNT; i++) {
                final int et = i;
                boolean sel = i == selectedEntityType;
                boolean enabled = (entityFeatures[i] & F_ENABLED) != 0;
                TextView item = text(ET_NAMES[i], 17, sel ? INK : enabled ? MUTED : 0xFF556677);
                item.setGravity(Gravity.CENTER);
                item.setBackground(shape(sel ? BG_ACTIVE : BG_CARD, 8));
                LayoutParams lp = new LayoutParams(0, 42, 1); lp.setMargins(0, 0, 6, 4);
                (i < 3 ? row1 : row2).addView(item, lp);
                item.setOnClickListener(new OnClickListener() {
                    public void onClick(View v) { selectedEntityType = et; selectTab(0); }
                });
            }
            body.addView(row1, new LayoutParams(-1, 46));
            body.addView(row2, new LayoutParams(-1, 46));
        }

        void etToggle(String name, final int flag, boolean enabled) {
            Switch control = new Switch(getContext()); control.setText(name); control.setTextColor(INK);
            control.setTextSize(TypedValue.COMPLEX_UNIT_PX, 20); control.setChecked(enabled);
            control.setPadding(12, 0, 14, 0);
            control.setShowText(false); control.setSplitTrack(false); control.setSwitchMinWidth(60);
            styleSwitch(control);
            body.addView(control, new LayoutParams(-1, 50));
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
            section("Visibility colors", "Pick a type and state. Unknown visibility is amber.");
            LinearLayout targets = row();
            for (int i = 0; i < ET_COUNT; i++) {
                final int slot = i;
                boolean sel = i == colorSlot;
                TextView target = text(ET_NAMES[i], 16, sel ? INK : MUTED);
                target.setGravity(Gravity.CENTER);
                target.setBackground(shape(sel ? BG_ACTIVE : BG_CARD, 8));
                LayoutParams lp = new LayoutParams(0, 42, 1); lp.setMargins(0, 0, 4, 0);
                targets.addView(target, lp);
                target.setOnClickListener(new OnClickListener() {
                    public void onClick(View v) { colorSlot = slot; selectTab(1); }
                });
            }
            body.addView(targets);
            if (colorSlot == ET_LOOT) { lootSettingsLink(); return; }

            LinearLayout states = row();
            for (int state = 0; state < 2; state++) {
                final boolean visible = state == 0;
                TextView option = text(visible ? "Visible" : "Occluded", 20,
                    colorVisible == visible ? INK : MUTED);
                option.setGravity(Gravity.CENTER);
                option.setBackground(shape(colorVisible == visible ? BG_ACTIVE : BG_CARD, 8));
                LayoutParams lp = new LayoutParams(0, 44, 1); lp.setMargins(0, 10, 6, 0);
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
            section("Line weight", null);
            SeekBar weight = new SeekBar(getContext()); weight.setMax(40);
            weight.setProgress(Math.round((lineWidth - 1) * 10));
            styleSlider(weight);
            body.addView(weight, new LayoutParams(-1, 42));
            weight.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                public void onProgressChanged(SeekBar bar, int value, boolean user) {
                    if (user) lineWidth = 1 + value / 10f;
                }
                public void onStartTrackingTouch(SeekBar bar) {}
                public void onStopTrackingTouch(SeekBar bar) { saveSettings(); }
            });
        }

        void lootSettingsLink() {
            section("Loot rarity colors", "Loot uses the game's item colors. Configure filters in Loot.");
            TextView link = text("Open loot settings", 20, ACCENT);
            link.setPadding(12, 14, 12, 14); body.addView(link, new LayoutParams(-1, 56));
            link.setOnClickListener(new OnClickListener() { public void onClick(View v) { selectTab(5); } });
        }
        void buildLootTab() {
            section("Loot", "Names and colors come from the game's item data.");
            final int oldType = selectedEntityType; selectedEntityType = ET_LOOT;
            etToggle("Show loot", F_ENABLED, (entityFeatures[ET_LOOT] & F_ENABLED) != 0);
            etToggle("Item names & stack counts", F_NAMES, (entityFeatures[ET_LOOT] & F_NAMES) != 0);
            etToggle("Distance labels", F_DISTANCE, (entityFeatures[ET_LOOT] & F_DISTANCE) != 0);
            etToggle("Item markers", F_BOXES, (entityFeatures[ET_LOOT] & F_BOXES) != 0);
            selectedEntityType = oldType;
            toggle("Show reference value per item", -15, lootShowValue);
            toggle("Include containers, stashes & death boxes", -16, lootShowContainers);
            section("Rarity", "Selected rarities and minimum value both apply. Unknown values fail a positive threshold.");
            for (int q = 0; q < LOOT_RARITIES.length; ++q) {
                final int quality = q;
                Switch control = new Switch(getContext()); control.setText(LOOT_RARITIES[q]);
                control.setTag(Integer.valueOf(q)); control.setTextColor(lootGameColors[q]);
                control.setTextSize(TypedValue.COMPLEX_UNIT_PX, 20);
                control.setChecked((lootRarityMask & (1 << q)) != 0);
                control.setPadding(12,0,14,0); control.setShowText(false); control.setSplitTrack(false);
                styleSwitch(control); body.addView(control, new LayoutParams(-1,50));
                control.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
                    public void onCheckedChanged(CompoundButton button, boolean checked) {
                        if (checked) lootRarityMask |= 1 << quality; else lootRarityMask &= ~(1 << quality);
                        saveSettings();
                    }
                });
            }
            final TextView minimum = text("Minimum reference value / item: " + lootMinValue, 20, INK);
            body.addView(minimum, new LayoutParams(-1,48));
            SeekBar value = new SeekBar(getContext()); value.setMax(1000); value.setProgress(lootMinValue / 1000);
            styleSlider(value); body.addView(value, new LayoutParams(-1,42));
            value.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                public void onProgressChanged(SeekBar b,int n,boolean user) {
                    if (user) { lootMinValue=n*1000; minimum.setText("Minimum reference value / item: " + lootMinValue); }
                }
                public void onStartTrackingTouch(SeekBar b) {}
                public void onStopTrackingTouch(SeekBar b) { saveSettings(); }
            });
            final TextView distance = text("Loot render distance: " + entityRenderDist[ET_LOOT] + "m", 20, INK);
            body.addView(distance, new LayoutParams(-1,48));
            SeekBar range = new SeekBar(getContext()); range.setMax(100); range.setProgress(entityRenderDist[ET_LOOT] / 10);
            styleSlider(range); body.addView(range, new LayoutParams(-1,42));
            range.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                public void onProgressChanged(SeekBar b,int n,boolean user) {
                    if (user) { entityRenderDist[ET_LOOT]=Math.max(10,n*10); distance.setText("Loot render distance: " + entityRenderDist[ET_LOOT] + "m"); }
                }
                public void onStartTrackingTouch(SeekBar b) {}
                public void onStopTrackingTouch(SeekBar b) { saveSettings(); }
            });
            section("Value source", "Game reference unit value. Live market quotes and container contents are not inferred.");
        }

        /* ---- Aim tab ---- */
        void buildAimTab() {
            section("Aim assist", "Locks onto a live, visible target inside the circle. Disable Aim to release.");
            toggle("Enable aim", -2, aimEnabled);
            toggle("Show lock button", -11, showLockBtn);

            section("Trigger bot", "Holds fire while a visible, living enemy is under the crosshair. Works with Aim off.");
            toggle("Auto shoot", -3, autoShoot);
            toggle("Auto zoom before shooting", -10, autoZoom);

            section("Target types", null);
            aimTargetToggle("Enemy players", AIM_ENEMIES);
            aimTargetToggle("Teammates", AIM_TEAMMATES);
            aimTargetToggle("Bots", AIM_BOTS);

            section("Aim point", null);
            buildBonePicker();

            section("Target priority", null);
            buildPriorityPicker();

            section("FOV radius", aim360 ? "360°" : aimFov + "°");
            if (!aim360) {
                SeekBar fov = new SeekBar(getContext()); fov.setMax(40); fov.setProgress(aimFov - 5);
                styleSlider(fov);
                body.addView(fov, new LayoutParams(-1, 42));
                fov.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                    public void onProgressChanged(SeekBar bar, int value, boolean user) {
                        if (user) aimFov = value + 5;
                    }
                    public void onStartTrackingTouch(SeekBar bar) {}
                    public void onStopTrackingTouch(SeekBar bar) { saveSettings(); selectTab(2); }
                });
            }
            toggle("360° (lock any visible)", -12, aim360);


        }

        void aimTargetToggle(String name, final int flag) {
            Switch control = new Switch(getContext()); control.setText(name); control.setTextColor(INK);
            control.setTextSize(TypedValue.COMPLEX_UNIT_PX, 20);
            control.setChecked((aimTargetMask & flag) != 0);
            control.setPadding(12, 0, 14, 0);
            control.setShowText(false); control.setSplitTrack(false); control.setSwitchMinWidth(60);
            styleSwitch(control);
            body.addView(control, new LayoutParams(-1, 50));
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
                LayoutParams lp = new LayoutParams(0, 44, 1); lp.setMargins(0, 0, 8, 0);
                row.addView(item, lp);
                item.setOnClickListener(new OnClickListener() {
                    public void onClick(View v) { aimBone = bone; saveSettings(); selectTab(2); }
                });
            }
            body.addView(row, new LayoutParams(-1, 48));
        }

        void buildPriorityPicker() {
            String[] labels = {"Crosshair", "Distance", "Low HP", "Facing me"};
            LinearLayout row = row();
            for (int i = 0; i < 4; i++) {
                final int prio = i;
                TextView item = text(labels[i], 17, i == aimPriority ? INK : MUTED);
                item.setGravity(Gravity.CENTER);
                item.setBackground(shape(i == aimPriority ? BG_ACTIVE : BG_CARD, 8));
                LayoutParams lp = new LayoutParams(0, 44, 1); lp.setMargins(0, 0, 8, 0);
                row.addView(item, lp);
                item.setOnClickListener(new OnClickListener() {
                    public void onClick(View v) { aimPriority = prio; saveSettings(); selectTab(2); }
                });
            }
            body.addView(row, new LayoutParams(-1, 48));
        }

        /* ---- Misc tab ---- */
        void buildMiscTab() {
            section("Weapon", null);
            toggle("No recoil", -8, noRecoil);
            toggle("No spread", -9, noSpread);

            section("Movement", null);
            toggle("Infinite stamina", -14, infiniteStamina);

            section("Camera FOV", cameraFov == 0 ? "Default" : cameraFov + "°");
            SeekBar fovSlider = new SeekBar(getContext()); fovSlider.setMax(160); fovSlider.setProgress(cameraFov == 0 ? 80 : cameraFov - 10);
            styleSlider(fovSlider);
            body.addView(fovSlider, new LayoutParams(-1, 42));
            fovSlider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                public void onProgressChanged(SeekBar bar, int value, boolean user) {
                    if (user) cameraFov = value + 10;
                }
                public void onStartTrackingTouch(SeekBar bar) {}
                public void onStopTrackingTouch(SeekBar bar) { saveSettings(); selectTab(3); }
            });
            toggle("Reset to default", -13, cameraFov == 0);
        }

        /* ---- Status tab ---- */
        void buildStatusTab() {
            section("Raycast debug", "Collected collision bounds; not an engine physics trace.");
            toggle("Enable raycast debug", -4, rayDebug);
            toggle("Actor eye / view rays (50 m)", -5, rayViews);
            toggle("Camera LOS: five body samples", -6, rayLos);
            toggle("Draw hit collision bounds", -7, rayBounds);
            TextView note = text("Actor rays use controller rotation, or labelled body rotation when unavailable. " +
                "LOS links start at screen centre; markers show the first bound hit. " +
                "Names fall back to class/object IDs. Up to 32 nearest actors within 100 m.", 17, MUTED);
            note.setPadding(12, 8, 12, 12); body.addView(note, new LayoutParams(-1, -2));
            section("Live diagnostics", "A frame expires automatically when updates stop.");
            status = text("", 19, MUTED); status.setTypeface(Typeface.MONOSPACE);
            status.setGravity(Gravity.TOP); status.setPadding(12, 14, 12, 14);
            status.setBackground(shape(0xFF0B121D, 12));
            body.addView(status, new LayoutParams(-1, -2)); refreshStatus();
        }

        /* ---- Common toggle ---- */
        void toggle(String name, final int key, boolean enabled) {
            Switch control = new Switch(getContext()); control.setText(name); control.setTextColor(INK);
            control.setTextSize(TypedValue.COMPLEX_UNIT_PX, 21); control.setChecked(enabled);
            control.setPadding(12, 0, 14, 0);
            control.setShowText(false); control.setSplitTrack(false); control.setSwitchMinWidth(66);
            styleSwitch(control);
            body.addView(control, new LayoutParams(-1, 54));
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
                        case -12: aim360 = checked; break;
                        case -14: infiniteStamina = checked; break;
                        case -15: lootShowValue = checked; break;
                        case -17: chamsEnabled = checked; saveSettings(); selectTab(0); return;
                        case -16: lootShowContainers = checked; break;
                        case -13: cameraFov = checked ? 0 : 90; saveSettings(); selectTab(3); break;
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
                new int[] {0xFF27785F, 0xFF2B3B50}));
            control.setThumbTintList(new android.content.res.ColorStateList(
                new int[][] {new int[] {android.R.attr.state_checked}, new int[] {}},
                new int[] {ACCENT, MUTED}));
        }

        void styleSlider(SeekBar slider) {
            GradientDrawable track = shape(0xFF2B3B50, 4); track.setSize(64, 8);
            GradientDrawable progress = shape(ACCENT, 4); progress.setSize(64, 8);
            LayerDrawable layers = new LayerDrawable(new android.graphics.drawable.Drawable[] {
                track, new ClipDrawable(progress, Gravity.LEFT, ClipDrawable.HORIZONTAL)});
            layers.setId(0, android.R.id.background); layers.setId(1, android.R.id.progress);
            slider.setProgressDrawable(layers);
            GradientDrawable thumb = shape(0xFFE7EDF5, 12); thumb.setSize(24, 24);
            slider.setThumb(thumb); slider.setThumbOffset(12); slider.setPadding(12, 0, 12, 0);
        }

        void updateSwatch(TextView swatch) {
            int color = gvc(colorSlot, colorVisible);
            swatch.setText(String.format("%s · %s   #%06X", ET_NAMES[colorSlot],
                colorVisible ? "Visible" : "Occluded", color & 0xFFFFFF));
            swatch.setTextColor(Color.luminance(color) > 0.4f ? 0xFF101825 : Color.WHITE);
            swatch.setBackground(shape(color, 12));
        }

        void refreshStatus() {
            if (status == null) return;
            long age = frameAt == 0 ? -1 : SystemClock.uptimeMillis() - frameAt;
            status.setText("Viewport    " + sw + " × " + sh +
                "\nFrame age   " + (age < 0 ? "waiting" : age + " ms") +
                "\nEntities    " + diagEntities + "\nWorld       " + diagGWorld +
                "\nName pool   " + diagFName + "\nReader      " + diagStatus +
                (diagExtra.length() == 0 ? "" : "\n" + diagExtra));
        }
    }
}
