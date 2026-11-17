package com.rdzim.memorycheck;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.ColorStateList;
import android.graphics.*;
import android.graphics.drawable.*;
import android.view.Gravity;
import android.view.View;
import android.widget.*;
import java.lang.ref.WeakReference;
public final class MenuLook {
    public static final int ACCENT=0xffada2ff,INK=0xfff5f7ff,MUTED=0xffa6b1cc;
    private static WeakReference<Bitmap> coverCache=new WeakReference<>(null);
    private MenuLook(){}
    public static int dp(Context c,int n){return Math.round(n*c.getResources().getDisplayMetrics().density);}
    public static int preset(SharedPreferences p){return Math.max(0,Math.min(2,p.getInt("menu_size",0)));}
    public static int width(SharedPreferences p){return new int[]{240,270,300}[preset(p)];}
    public static int height(SharedPreferences p){return new int[]{300,350,400}[preset(p)];}
    public static GradientDrawable rounded(Context c,int color,int radius){GradientDrawable d=new GradientDrawable();d.setColor(color);d.setCornerRadius(dp(c,radius));return d;}
    public static GradientDrawable gradient(Context c,int first,int last,int radius){GradientDrawable d=new GradientDrawable(GradientDrawable.Orientation.TL_BR,new int[]{first,last});d.setCornerRadius(dp(c,radius));return d;}
    public static TextView text(Context c,String value,int size,int color){TextView t=new TextView(c);t.setText(value);t.setTextColor(color);t.setTextSize(size);return t;}
    public static Button button(Context c,String value){Button b=new Button(c);b.setText(value);b.setTextSize(12);b.setAllCaps(false);b.setGravity(Gravity.CENTER);b.setTextColor(INK);b.setMinWidth(0);b.setMinimumWidth(0);b.setMinHeight(0);b.setMinimumHeight(0);b.setPadding(dp(c,8),0,dp(c,8),0);b.setBackground(new RippleDrawable(ColorStateList.valueOf(0x28ffffff),rounded(c,0xff242d48,16),rounded(c,Color.WHITE,16)));return b;}
    public static Button action(Context c,String value,boolean bright){Button b=button(c,value);b.setTextSize(15);b.setTypeface(null,Typeface.BOLD);GradientDrawable bg=bright?gradient(c,0xff9867ff,0xff507cf4,24):gradient(c,0xff16464f,0xff15374c,24);if(!bright)bg.setStroke(dp(c,1),0xff378e96);b.setTextColor(bright?Color.WHITE:0xff83f8db);b.setBackground(new RippleDrawable(ColorStateList.valueOf(0x30ffffff),bg,rounded(c,Color.WHITE,24)));return b;}
    public static LinearLayout card(Context c){LinearLayout l=new LinearLayout(c){@Override protected void onMeasure(int w,int h){int width=MeasureSpec.getSize(w);if(MeasureSpec.getMode(w)!=MeasureSpec.UNSPECIFIED)w=MeasureSpec.makeMeasureSpec(Math.min(width,dp(getContext(),420)),MeasureSpec.EXACTLY);super.onMeasure(w,h);}};l.setOrientation(LinearLayout.VERTICAL);GradientDrawable bg=gradient(c,0xf01a2241,0xf00e172c,32);bg.setStroke(dp(c,1),0xff3a4367);l.setBackground(bg);l.setClipToOutline(true);l.setElevation(dp(c,12));return l;}
    public static ImageView cover(Context c){ImageView v=new ImageView(c);Bitmap image=coverCache.get();if(image==null||image.isRecycled()){try(java.io.InputStream in=c.getAssets().open("lucas_cover.png")){image=BitmapFactory.decodeStream(in);coverCache=new WeakReference<>(image);}catch(Exception ignored){}}v.setImageBitmap(image);v.setScaleType(ImageView.ScaleType.CENTER_CROP);v.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);return v;}
    public static FrameLayout backdrop(Context c){FrameLayout frame=new FrameLayout(c);frame.setBackgroundColor(0xff080d1b);ImageView image=cover(c);image.setImageAlpha(140);frame.addView(image,new FrameLayout.LayoutParams(-1,-1));View shade=new View(c);shade.setBackground(new GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM,new int[]{0x65080d1b,0xb8080d1b,0xff080d1b}));frame.addView(shade,new FrameLayout.LayoutParams(-1,-1));return frame;}
    public static ImageView portrait(Context c,boolean round){ImageView v=new ImageView(c);v.setImageResource(c.getResources().getIdentifier("lucas_launcher_photo","drawable",c.getPackageName()));v.setScaleType(ImageView.ScaleType.CENTER_CROP);GradientDrawable bg=gradient(c,0xff8f75ff,0xff53d9f5,16);if(round)bg.setShape(GradientDrawable.OVAL);v.setBackground(bg);v.setPadding(dp(c,2),dp(c,2),dp(c,2),dp(c,2));v.setClipToOutline(true);v.setContentDescription("Foto Arroz Menu");return v;}
    public static LinearLayout sizes(Context c,SharedPreferences p){LinearLayout row=new LinearLayout(c);String[] names={"Compacto","Médio","Grande"};Button[] buttons=new Button[3];for(int i=0;i<3;i++){final int index=i;Button b=button(c,names[i]);b.setTextSize(11);LinearLayout.LayoutParams lp=new LinearLayout.LayoutParams(0,dp(c,36),1);if(i>0)lp.leftMargin=dp(c,5);row.addView(b,lp);buttons[i]=b;b.setOnClickListener(v->{p.edit().putInt("menu_size",index).apply();for(int k=0;k<3;k++)select(buttons[k],k==index);});}for(int i=0;i<3;i++)select(buttons[i],i==preset(p));return row;}
    public static void refreshSizes(LinearLayout row,SharedPreferences p){for(int i=0;i<row.getChildCount();i++)select((Button)row.getChildAt(i),i==preset(p));}
    public static void select(Button b,boolean selected){Context c=b.getContext();GradientDrawable d=selected?gradient(c,0xff3e3474,0xff263965,16):rounded(c,0xff1d2740,16);if(selected)d.setStroke(dp(c,1),0xff8974d7);b.setBackground(new RippleDrawable(ColorStateList.valueOf(0x28ffffff),d,rounded(c,Color.WHITE,16)));b.setTextColor(selected?0xffddd8ff:MUTED);b.setTypeface(null,selected?Typeface.BOLD:Typeface.NORMAL);}
    public static ColorStateList thumb(){return new ColorStateList(new int[][]{new int[]{android.R.attr.state_checked},new int[]{}},new int[]{0xff83e9e4,0xff64718a});}
}
