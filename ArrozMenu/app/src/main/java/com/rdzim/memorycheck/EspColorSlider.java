package com.rdzim.memorycheck;

import android.content.Context;
import android.graphics.*;
import android.graphics.drawable.*;
import android.widget.SeekBar;

final class EspColorSlider extends SeekBar {
    private final Paint track=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final int[] stops=new int[15];
    EspColorSlider(Context context){super(context);setMax(EspPalette.MAX);setThumbTintList(null);setProgressTintList(null);setProgressBackgroundTintList(null);setProgressDrawable(new ColorDrawable(Color.TRANSPARENT));setPadding(MenuLook.dp(context,12),0,MenuLook.dp(context,12),0);for(int i=0;i<stops.length;i++)stops[i]=EspPalette.color(i*30);setSplitTrack(false);setSelectionColor(0xff79ffb7);setContentDescription("Arraste para alterar a cor da ESP");}
    void setSelectionColor(int color){GradientDrawable thumb=new GradientDrawable();thumb.setShape(GradientDrawable.OVAL);thumb.setColor(color);thumb.setStroke(MenuLook.dp(getContext(),2),Color.WHITE);int size=MenuLook.dp(getContext(),22);thumb.setSize(size,size);setThumb(thumb);setThumbOffset(size/2);setContentDescription(String.format(java.util.Locale.ROOT,"Cor da ESP #%06X. Arraste para mudar.",color&0xffffff));invalidate();}
    @Override protected void onSizeChanged(int w,int h,int oldW,int oldH){super.onSizeChanged(w,h,oldW,oldH);if(w>getPaddingLeft()+getPaddingRight())track.setShader(new LinearGradient(getPaddingLeft(),0,w-getPaddingRight(),0,stops,null,Shader.TileMode.CLAMP));}
    @Override protected synchronized void onDraw(Canvas canvas){float cy=getHeight()*.5f,r=MenuLook.dp(getContext(),4);canvas.drawRoundRect(getPaddingLeft(),cy-r,getWidth()-getPaddingRight(),cy+r,r,r,track);super.onDraw(canvas);}
}
