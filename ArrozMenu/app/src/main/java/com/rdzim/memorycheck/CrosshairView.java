package com.rdzim.memorycheck;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.view.View;

public class CrosshairView extends View {
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final SharedPreferences prefs;
    public CrosshairView(Context context) {
        super(context);
        prefs=context.getSharedPreferences("crosshair",Context.MODE_PRIVATE);
        setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
    }
    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float density=getResources().getDisplayMetrics().density;
        float x=getWidth()/2f,y=getHeight()/2f;
        float arm=Math.max(6,Math.min(28,prefs.getInt("size",14)))*density;
        float gap=4*density;
        paint.setStrokeWidth(2*density);
        paint.setStrokeCap(Paint.Cap.SQUARE);
        paint.setStyle(Paint.Style.STROKE);
        paint.setColor(prefs.getInt("color",Color.rgb(121,242,191)));
        if(prefs.getBoolean("circle",false)) {
            canvas.drawCircle(x,y,arm,paint);
        } else {
            canvas.drawLine(x-gap-arm,y,x-gap,y,paint);
            canvas.drawLine(x+gap,y,x+gap+arm,y,paint);
            canvas.drawLine(x,y-gap-arm,x,y-gap,paint);
            canvas.drawLine(x,y+gap,x,y+gap+arm,paint);
        }
        paint.setStyle(Paint.Style.FILL);
        canvas.drawCircle(x,y,density*1.5f,paint);
    }
}
