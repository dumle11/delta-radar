import android.app.Activity;
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
import android.view.WindowManager;
import java.util.concurrent.atomic.AtomicInteger;

public class O extends View {
    static volatile Bitmap front, back;
    static Paint ps, pf, pt, bp;
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
    static final long FRAME_TTL_MS = 500;
    static volatile boolean rdy;
    static Canvas cv, frontCanvas, backCanvas;
    static O inst;
    static WindowManager wm;
    static Handler uiHandler;
    static volatile boolean initDone;

    static BubbleView bubble;
    static MenuPanel menu;

    static SharedPreferences prefs;
    static volatile boolean boxesEnabled = true, skeletonEnabled = false, namesEnabled = true;
    static volatile float lineWidth = 2.5f;
    static final int[] defaultColors = {0xFFE5534B, 0xFF46D9FF, 0xFF4FD08A, 0xFF4BCA81};
    static volatile int playerColor = defaultColors[0], botColor = defaultColors[1],
        allyColor = defaultColors[2], lootColor = defaultColors[3];
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

    static synchronized void z(final Activity a) {
        if (initDone) return;
        initDone = true;
        uiHandler = new Handler(Looper.getMainLooper());
        a.runOnUiThread(new Runnable() {
            public void run() {
                try {
                    prefs = a.getSharedPreferences("overlay_visuals", Context.MODE_PRIVATE);
                    espEnabled = prefs.getBoolean("enabled", true);
                    boxesEnabled = prefs.getBoolean("boxes", true);
                    skeletonEnabled = prefs.getBoolean("skeleton", false);
                    namesEnabled = prefs.getBoolean("names", true);
                    healthBars = prefs.getBoolean("health", true);
                    distLabels = prefs.getBoolean("distance", true);
                    snaplines = prefs.getBoolean("snaplines", false);
                    lineWidth = prefs.getFloat("width", 2.5f);
                    menuX = prefs.getInt("menuX", -1); menuY = prefs.getInt("menuY", -1);
                    for (int i = 0; i < 4; i++) setColor(i, prefs.getInt("color" + i, defaultColors[i]));
                    wm = (WindowManager) a.getSystemService(Context.WINDOW_SERVICE);
                    sh = a.getResources().getDisplayMetrics().heightPixels;

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
                    uiHandler.post(new Runnable() {
                        public void run() {
                            if (inst == null) return;
                            if (frameAt != 0 && SystemClock.uptimeMillis() - frameAt > FRAME_TTL_MS)
                                inst.invalidate();
                            uiHandler.postDelayed(this, 250);
                        }
                    });
                } catch (Exception ex) {
                    initDone = false;
                }
            }
        });
    }

    @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        viewWidth = w;
        viewHeight = h;
        frameAt = 0; // Coordinates from the old viewport are no longer usable.
    }

    @Override protected void onDraw(Canvas c) {
        if (!espEnabled || frameAt == 0) return;
        // Pin a completed frame. The producer never waits for the UI and never
        // modifies a published or pinned bitmap. A frame in progress cannot blank it.
        for (int attempt = 0; attempt < 4; attempt++) {
            Frame f = publishedFrame;
            if (f == null) return;
            f.readers.incrementAndGet();
            try {
                if (f != publishedFrame) continue;
                if (f.width != viewWidth || f.height != viewHeight || f.presentedAt == 0 ||
                    SystemClock.uptimeMillis() - f.presentedAt > FRAME_TTL_MS) return;
                c.drawBitmap(f.bitmap, 0, 0, bp);
                return;
            } finally { f.readers.decrementAndGet(); }
        }
    }

    /* One producer, three preallocated frames, independently pinned UI reads. */
    static boolean b() {
        if (!rdy || cv != null) return false;
        int w = viewWidth, h = viewHeight;
        if (w <= 0 || h <= 0) return false;
        if (frames == null || frames[0].width != w || frames[0].height != h) {
            Frame[] resized = {new Frame(w, h), new Frame(w, h), new Frame(w, h)};
            frames = resized; sw = w; sh = h;
            // Old bitmaps stay alive for any UI reader; GC reclaims them safely.
        }
        Frame target = null;
        for (Frame candidate : frames) {
            if (candidate != publishedFrame && candidate.readers.get() == 0) {
                target = candidate; break;
            }
        }
        if (target == null) return false; // Drop production, preserve the completed frame.
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
    static void t(String txt, float x, float y, int c) {
        if (cv == null) return; pt.setColor(c); cv.drawText(txt, x, y, pt);
    }
    static void e() {
        Frame complete = drawingFrame;
        if (cv == null || complete == null) return;
        cv = null; drawingFrame = null;
        complete.presentedAt = (complete.width == viewWidth && complete.height == viewHeight)
            ? SystemClock.uptimeMillis() : 0;
        front = complete.bitmap; frontCanvas = complete.canvas;
        frameAt = complete.presentedAt;
        publishedFrame = complete;
        if (inst != null) inst.postInvalidateOnAnimation();
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
    static boolean gb() { return boxesEnabled; }
    static boolean gk() { return skeletonEnabled; }
    static boolean gn() { return namesEnabled; }
    static int gc(int slot) {
        return slot == 1 ? botColor : slot == 2 ? allyColor : slot == 3 ? lootColor : playerColor;
    }
    static void setColor(int slot, int color) {
        if (slot == 1) botColor = color;
        else if (slot == 2) allyColor = color;
        else if (slot == 3) lootColor = color;
        else playerColor = color;
    }
    static void saveSettings() {
        if (prefs == null) return;
        SharedPreferences.Editor edit = prefs.edit().putBoolean("enabled", espEnabled)
            .putBoolean("boxes", boxesEnabled).putBoolean("skeleton", skeletonEnabled)
            .putBoolean("names", namesEnabled).putBoolean("health", healthBars)
            .putBoolean("distance", distLabels).putBoolean("snaplines", snaplines)
            .putFloat("width", lineWidth).putInt("menuX", menuX).putInt("menuY", menuY);
        for (int i = 0; i < 4; i++) edit.putInt("color" + i, gc(i));
        edit.apply();
    }
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
                Math.min(760, Math.max(280, viewWidth - 120)),
                Math.min(700, Math.max(280, viewHeight - 80)),
                WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
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

    /* ======== Tabbed visual controls ======== */
    static class MenuPanel extends LinearLayout {
        static final int INK = 0xFFE7EDF5, MUTED = 0xFF94A3B8, ACCENT = 0xFF5EE2B0;
        LinearLayout body, nav;
        TextView status, summary, dragHandle;
        int selectedTab, colorSlot;
        final String[] colorNames = {"Players", "Bots", "Teammates", "Loot"};

        MenuPanel(Context context) {
            super(context);
            setOrientation(VERTICAL); setPadding(24, 20, 24, 20);
            setDescendantFocusability(FOCUS_BLOCK_DESCENDANTS);
            setBackground(shape(0xF5101825, 24));
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
            summary = text("Drag the header to move · changes apply immediately", 18, MUTED);
            addView(summary, new LayoutParams(-1, 42));
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
            GradientDrawable background = new GradientDrawable(); background.setColor(color);
            background.setCornerRadius(radius); return background;
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
            body.addView(heading, new LayoutParams(-1, 40));
            if (subtitle != null) body.addView(text(subtitle, 17, MUTED), new LayoutParams(-1, 36));
        }
        void selectTab(int tab) {
            selectedTab = tab; nav.removeAllViews(); body.removeAllViews(); status = null;
            String[] titles = {"Visuals", "Colors", "Status"};
            for (int i = 0; i < titles.length; i++) {
                final int target = i;
                TextView item = text(titles[i], 21, i == tab ? 0xFF0C201A : MUTED);
                item.setGravity(Gravity.CENTER); item.setTypeface(Typeface.DEFAULT_BOLD);
                item.setBackground(shape(i == tab ? ACCENT : 0xFF1D2939, 12));
                LayoutParams lp = new LayoutParams(0, -1, 1); lp.setMargins(0, 0, 8, 0);
                nav.addView(item, lp);
                item.setOnClickListener(new OnClickListener() { public void onClick(View v) { selectTab(target); } });
            }
            if (tab == 0) {
                section("Make the scene readable", "Choose the information you want to see.");
                toggle("Enable visuals", 0, espEnabled);
                toggle("Bounding boxes", 1, boxesEnabled);
                toggle("Skeleton", 2, skeletonEnabled);
                toggle("Names & types", 3, namesEnabled);
                toggle("Health bars", 4, healthBars);
                toggle("Distance", 5, distLabels);
                toggle("Snap lines", 6, snaplines);
            } else if (tab == 1) {
                section("Your visual palette", "Pick a category, then tune its color.");
                LinearLayout targets = row();
                for (int i = 0; i < colorNames.length; i++) {
                    final int slot = i;
                    TextView target = text(colorNames[i], 18, i == colorSlot ? INK : MUTED);
                    target.setGravity(Gravity.CENTER);
                    target.setBackground(shape(i == colorSlot ? 0xFF30455B : 0xFF1D2939, 8));
                    targets.addView(target, new LayoutParams(0, 46, 1));
                    target.setOnClickListener(new OnClickListener() {
                        public void onClick(View v) { colorSlot = slot; selectTab(1); }
                    });
                }
                body.addView(targets);
                final TextView swatch = text("", 22, INK); swatch.setGravity(Gravity.CENTER);
                LayoutParams swatchLp = new LayoutParams(-1, 64); swatchLp.setMargins(0, 18, 0, 12);
                body.addView(swatch, swatchLp);
                final float[] hsv = new float[3]; Color.colorToHSV(gc(colorSlot), hsv);
                updateSwatch(swatch);
                String[] components = {"Hue", "Saturation", "Brightness"};
                for (int i = 0; i < 3; i++) {
                    final int component = i;
                    body.addView(text(components[i], 18, MUTED), new LayoutParams(-1, 30));
                    SeekBar slider = new SeekBar(getContext()); slider.setMax(i == 0 ? 360 : 100);
                    slider.setProgress(Math.round(hsv[i] * (i == 0 ? 1 : 100)));
                    styleSlider(slider);
                    body.addView(slider, new LayoutParams(-1, 42));
                    slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                        public void onProgressChanged(SeekBar bar, int value, boolean user) {
                            if (!user) return;
                            hsv[component] = component == 0 ? value : value / 100f;
                            setColor(colorSlot, Color.HSVToColor(hsv)); updateSwatch(swatch);
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
            } else {
                section("Live diagnostics", "A frame expires automatically when updates stop.");
                status = text("", 19, MUTED); status.setTypeface(Typeface.MONOSPACE);
                status.setGravity(Gravity.TOP); status.setPadding(12, 14, 12, 14);
                status.setBackground(shape(0xFF0B121D, 12));
                body.addView(status, new LayoutParams(-1, -2)); refreshStatus();
            }
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
            int color = gc(colorSlot);
            swatch.setText(String.format("%s   #%06X", colorNames[colorSlot], color & 0xFFFFFF));
            swatch.setTextColor(Color.luminance(color) > 0.4f ? 0xFF101825 : Color.WHITE);
            swatch.setBackground(shape(color, 12));
        }
        void toggle(String name, final int key, boolean enabled) {
            Switch control = new Switch(getContext()); control.setText(name); control.setTextColor(INK);
            control.setTextSize(TypedValue.COMPLEX_UNIT_PX, 21); control.setChecked(enabled);
            control.setPadding(12, 0, 14, 0);
            control.setShowText(false); control.setSplitTrack(false); control.setSwitchMinWidth(66);
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
            body.addView(control, new LayoutParams(-1, 57));
            control.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
                public void onCheckedChanged(CompoundButton button, boolean checked) {
                    switch (key) {
                        case 0: espEnabled = checked; break;
                        case 1: boxesEnabled = checked; break;
                        case 2: skeletonEnabled = checked; break;
                        case 3: namesEnabled = checked; break;
                        case 4: healthBars = checked; break;
                        case 5: distLabels = checked; break;
                        case 6: snaplines = checked; break;
                    }
                    saveSettings(); if (inst != null) inst.invalidate();
                }
            });
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
