package io.github.alondero.aerogaugerecomp;

import android.graphics.*;
import android.view.*;
import android.util.SparseArray;

/** Independent pointer ownership allows steering, throttle and drift together. */
final class TouchControls extends View {
    private final GameActivity activity;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final SparseArray<PointF> points = new SparseArray<>();
    private final int mode;
    private int stickPointer = -1;
    private float stickX, stickY, cx, cy, radius, unit;
    private boolean menu, hidden;
    private int pressed;
    // Normalized to height so the layout fits phones/tablets in either landscape direction.
    private final String[] labels = {"A", "B", "Z", "R", "Start", "L", "C↑", "C↓", "C←", "C→"};
    private final int[] masks = {0x8000, 0x4000, 0x2000, 0x10, 0x1000, 0x20, 8, 4, 2, 1};
    private final RectF[] buttons = new RectF[labels.length];
    private final RectF menuButton = new RectF();

    TouchControls(GameActivity activity, int mode) {
        super(activity); this.activity = activity; this.mode = mode;
        setFocusable(false);
        setContentDescription("Game touch controls. Analog steering on the left; A, B and Z on the right.");
        for (int i = 0; i < buttons.length; i++) buttons[i] = new RectF();
    }
    private boolean controllerConnected() {
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice input = InputDevice.getDevice(id);
            if (input != null && ((input.getSources() & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD)) return true;
        }
        return false;
    }
    @Override protected void onSizeChanged(int width, int height, int oldWidth, int oldHeight) {
        unit = Math.min(height, width / 1.6f);
        radius = unit * .16f; cx = radius + unit * .10f; cy = height - radius - unit * .09f;
        float size = unit * .16f, right = width - unit * .09f, bottom = height - unit * .06f;
        place(0, right - size, bottom - size * 1.6f, size);
        place(1, right - size * 2.1f, bottom - size, size);
        place(2, right - size * 3.2f, bottom - size, size);
        place(3, right - size * 2.1f, bottom - size * 2.2f, size * .8f);
        buttons[4].set(width/2f - unit*.10f, height-unit*.15f, width/2f+unit*.10f, height-unit*.045f);
        place(5, unit*.10f, unit*.22f, size*.65f);
        float ccx = width-unit*.15f, ccy = unit*.23f, csize = unit*.065f;
        place(6, ccx, ccy-csize, csize); place(7, ccx, ccy+csize, csize);
        place(8, ccx-csize, ccy, csize); place(9, ccx+csize, ccy, csize);
        menuButton.set(width-unit*.34f, unit*.035f, width-unit*.08f, unit*.15f);
    }
    private void place(int index, float x, float y, float size) { buttons[index].set(x, y, x+size, y+size); }
    @Override protected void onDraw(Canvas canvas) {
        boolean newMenu = GameActivity.nativeMenuOpen();
        boolean newHidden = mode == 2 || (mode == 0 && controllerConnected());
        if ((newMenu && !menu) || (newHidden && !hidden)) release();
        menu = newMenu; hidden = newHidden;
        if (!menu) drawButton(canvas, menuButton, "Menu", false);
        if (!menu && !hidden) {
            paint.setColor(0x60355165); canvas.drawCircle(cx, cy, radius, paint);
            paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(2); paint.setColor(0xa0a0c5d0);
            canvas.drawCircle(cx, cy, radius, paint); paint.setStyle(Paint.Style.FILL);
            paint.setColor(0xaa59e2d0); canvas.drawCircle(cx+stickX*radius, cy-stickY*radius, radius*.36f, paint);
            for (int i = 0; i < buttons.length; i++) drawButton(canvas, buttons[i], labels[i], (pressed & masks[i]) != 0);
        }
        postInvalidateDelayed(100);
    }
    private void drawButton(Canvas canvas, RectF rect, String label, boolean down) {
        paint.setColor(down ? 0xc059e2d0 : 0x80355165); canvas.drawRoundRect(rect, unit*.035f, unit*.035f, paint);
        paint.setColor(down ? 0xff09242b : 0xffedf9ff); paint.setTextAlign(Paint.Align.CENTER);
        paint.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL)); paint.setTextSize(unit*.037f);
        canvas.drawText(label, rect.centerX(), rect.centerY()-(paint.ascent()+paint.descent())/2, paint);
    }
    @Override public boolean onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked(), index = event.getActionIndex();
        if (menu) return false;
        if (action == MotionEvent.ACTION_DOWN && menuButton.contains(event.getX(), event.getY())) {
            activity.showActions(); return true;
        }
        if (menu || hidden) return false; // SDL receives settings mouse/touch events.
        if (action == MotionEvent.ACTION_CANCEL) { release(); return true; }
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            int id = event.getPointerId(index);
            if (stickPointer == -1 && event.getX(index) < getWidth()*.42f && event.getY(index) > getHeight()*.40f) stickPointer = id;
        }
        points.clear();
        for (int i = 0; i < event.getPointerCount(); i++) {
            if ((action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) && i == index) {
                if (event.getPointerId(i) == stickPointer) { stickPointer = -1; stickX = stickY = 0; }
            } else points.put(event.getPointerId(i), new PointF(event.getX(i), event.getY(i)));
        }
        pressed = 0;
        for (int i = 0; i < points.size(); i++) {
            PointF p = points.valueAt(i);
            if (points.keyAt(i) == stickPointer) {
                float x = (p.x-cx)/radius, y = (cy-p.y)/radius;
                float length = (float)Math.sqrt(x*x+y*y), divisor = Math.max(1, length);
                stickX = length < .12f ? 0 : x/divisor;
                stickY = length < .12f ? 0 : y/divisor;
            } else for (int b = 0; b < buttons.length; b++) if (buttons[b].contains(p.x, p.y)) pressed |= masks[b];
        }
        GameActivity.nativeTouch(pressed, Math.round(stickX*80), Math.round(stickY*80));
        if (action == MotionEvent.ACTION_UP) performClick();
        invalidate(); return true;
    }
    @Override public boolean performClick() { super.performClick(); return true; }
    void release() {
        points.clear(); stickPointer = -1; stickX = stickY = 0; pressed = 0;
        GameActivity.nativeTouch(0, 0, 0); invalidate();
    }
}
