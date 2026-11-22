package com.oll.esp;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.view.View;

import java.util.List;

final class OverlayView extends View {
    private final Paint boxEnemy = new Paint();
    private final Paint boxMate = new Paint();
    private final Paint linePaint = new Paint();
    private final Paint headPaint = new Paint();
    private final Paint textPaint = new Paint();
    private volatile List<Target> targets;

    OverlayView(Context context) {
        super(context);
        boxEnemy.setStyle(Paint.Style.STROKE);
        boxEnemy.setColor(Color.RED);
        boxEnemy.setStrokeWidth(2f);
        boxMate.setStyle(Paint.Style.STROKE);
        boxMate.setColor(Color.GREEN);
        boxMate.setStrokeWidth(2f);
        linePaint.setStyle(Paint.Style.STROKE);
        linePaint.setColor(Color.argb(180, 255, 255, 0));
        linePaint.setStrokeWidth(1.5f);
        headPaint.setStyle(Paint.Style.FILL);
        headPaint.setColor(Color.RED);
        textPaint.setColor(Color.WHITE);
        textPaint.setTextSize(26f);
    }

    void setTargets(List<Target> list) {
        targets = list;
        postInvalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        List<Target> list = targets;
        if (list == null) return;
        for (Target t : list) {
            canvas.drawRect(t.l, t.t, t.r, t.b, t.enemy ? boxEnemy : boxMate);
            if (t.lines != null) {
                for (int i = 0; i + 3 < t.lines.length; i += 4) {
                    canvas.drawLine(t.lines[i], t.lines[i + 1],
                            t.lines[i + 2], t.lines[i + 3], linePaint);
                }
            }
            if (t.hx >= 0 && t.hy >= 0) canvas.drawCircle(t.hx, t.hy, 7f, headPaint);
            canvas.drawText((t.enemy ? "ENEMY " : "MATE ") + t.dist + "m",
                    t.l, t.t - 10f, textPaint);
        }
    }
}
