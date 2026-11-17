package com.rdzim.memorycheck;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.*;
import android.os.SystemClock;
import android.view.View;
public final class EspLinesView extends View {
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final SharedPreferences prefs;
    private static final long FRAME_TTL_MS=EspMotion.TTL_MS;
    private final EspMotion motion=new EspMotion();
    private static final float[] EMPTY_POINTS=new float[0];
    private static final String[] EMPTY_NAMES=new String[0];
    private static final Typeface LABEL_FONT=Typeface.create("sans-serif-medium",Typeface.NORMAL);
    private long received;
    private final Runnable expireFrame=()->{if(received!=0)postInvalidateOnAnimation();};
    private void changed(){postInvalidateOnAnimation();}
    private void frameArrived(){
        removeCallbacks(expireFrame);
        if(points.length!=0){long age=SystemClock.uptimeMillis()-received;
            postDelayed(expireFrame,Math.max(1,FRAME_TTL_MS-age));}
        changed();
    }
    @Override protected void onDetachedFromWindow(){removeCallbacks(expireFrame);super.onDetachedFromWindow();}
    private float[] points=new float[0],health=new float[0];
    private String[] names=new String[0];
    private int stride=3;
    private int excludedX,excludedY,excludedW,excludedH;
    // -1=head, then chest, hips, upper arms, upper legs, feet and neck.
    private static final int[][] EDGES={{-1,8},{8,0},{0,1},{8,2},{8,3},{1,4},{1,5},{4,6},{5,7}};
    public EspLinesView(Context c){super(c);prefs=c.getSharedPreferences("crosshair",Context.MODE_PRIVATE);setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);}
    public void update(float[] frame,long arrived){motion.clear();points=frame;received=arrived;stride=3;names=EMPTY_NAMES;health=EMPTY_POINTS;frameArrived();}
    public void updateRich(EspFrameCodec.Frame frame,long arrived){motion.update(frame,arrived,SystemClock.uptimeMillis());points=motion.sample(SystemClock.uptimeMillis());names=frame.names;health=frame.health;received=arrived;stride=33;frameArrived();}
    public void clear(){motion.clear();points=EMPTY_POINTS;names=EMPTY_NAMES;health=EMPTY_POINTS;received=0;removeCallbacks(expireFrame);changed();}
    public void excludeMenu(int x,int y,int w,int h){excludedX=x;excludedY=y;excludedW=w;excludedH=h;changed();}
    private void stroke(int color,float width){paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(width);paint.setColor(color);paint.setAlpha(255);paint.setStrokeCap(Paint.Cap.ROUND);paint.setStrokeJoin(Paint.Join.ROUND);}
    private void label(Canvas c,String text,float x,float baseline,float d,int color){
        if(text==null||text.isEmpty())return;
        paint.setTypeface(LABEL_FONT);paint.setTextSize(11*d);paint.setStyle(Paint.Style.FILL);
        float limit=Math.min(getWidth()-16*d,150*d);if(limit<=0)return;
        int count=paint.breakText(text,true,limit,null);
        if(count<text.length()){if(count>0 && Character.isHighSurrogate(text.charAt(count-1)))count--;text=text.substring(0,Math.max(0,count))+"…";}
        float width=paint.measureText(text),half=width*.5f+5*d;
        x=Math.max(half,Math.min(getWidth()-half,x));baseline=Math.max(14*d,Math.min(getHeight()-4*d,baseline));
        paint.setColor(0xdd151618);c.drawRoundRect(x-half,baseline-12*d,x+half,baseline+3*d,3*d,3*d,paint);
        paint.setColor(color);c.drawText(text,x-width*.5f,baseline,paint);
    }
    private void skeleton(Canvas c,int base,float d,int color){
        float bodyHeight=points[base+5]==1?Math.abs(points[base+1]-points[base+4])*getHeight():0;
        if(bodyHeight<4*d)return;
        for(int pass=0;pass<2;pass++){
            stroke(pass==0?0xbb101216:color,(pass==0?3f:1.3f)*d);
            for(int[] edge:EDGES){
                int a=base+(edge[0]<0?3:6+edge[0]*3),b=base+(edge[1]<0?3:6+edge[1]*3);
                if(points[a+2]!=1||points[b+2]!=1)continue;
                float ax=points[a]*getWidth(),ay=points[a+1]*getHeight(),bx=points[b]*getWidth(),by=points[b+1]*getHeight();
                if(Math.hypot(bx-ax,by-ay)>bodyHeight*1.25f)continue;
                c.drawLine(ax,ay,bx,by,paint);
            }
        }
    }
    private void healthBar(Canvas c,int row,float left,float top,float bottom,float d){
        if(row*2+1>=health.length)return;float hp=health[row*2],max=health[row*2+1];
        if(!Float.isFinite(hp)||!Float.isFinite(max)||max<=0||hp<0||hp>max)return;
        float ratio=hp/max,x=Math.max(d,left-7*d),height=bottom-top;
        if(height<3*d)return;
        paint.setStyle(Paint.Style.FILL);paint.setColor(0xee101216);c.drawRoundRect(x-d,top-d,x+4*d,bottom+d,2*d,2*d,paint);
        paint.setColor(0xff42454c);c.drawRect(x,top,x+3*d,bottom,paint);
        paint.setColor(ratio>.55f?0xff79ffb7:ratio>.25f?0xffffd769:0xffff6078);c.drawRect(x,bottom-height*ratio,x+3*d,bottom,paint);
    }
    @Override protected void onDraw(Canvas c){
        super.onDraw(c);c.save();c.clipOutRect(excludedX,excludedY,excludedX+excludedW,excludedY+excludedH);
        float d=getResources().getDisplayMetrics().density;int color=prefs.getInt("esp_color",0xff79ffb7);
        if(prefs.getBoolean("aim_enabled",false)&&prefs.getBoolean("aim_show_fov",true)){stroke(color,1.2f*d);paint.setAlpha(110);c.drawCircle(getWidth()*.5f,getHeight()*.5f,getHeight()*Math.max(0,Math.min(360,prefs.getInt("aim_fov",130)))/720f,paint);}
        if(!prefs.getBoolean("esp_enabled",false)||received==0||SystemClock.uptimeMillis()-received>=FRAME_TTL_MS){c.restore();return;}
        if(stride==33)points=motion.sample(SystemClock.uptimeMillis());
        boolean box=prefs.getBoolean("esp_box",true),bones=prefs.getBoolean("esp_skeleton",false),name=prefs.getBoolean("esp_name",true),life=prefs.getBoolean("esp_health",false),line=prefs.getBoolean("esp_line",true),distance=prefs.getBoolean("esp_distance",false);
        for(int i=0,row=0;i+stride<=points.length;i+=stride,row++){
            float x=points[i]*getWidth(),y=points[i+1]*getHeight();
            if(line){stroke(0xbb101216,3*d);c.drawLine(getWidth()*.5f,getHeight()-12*d,x,y,paint);stroke(color,1.3f*d);c.drawLine(getWidth()*.5f,getHeight()-12*d,x,y,paint);}
            if(stride==33){
                if(bones)skeleton(c,i,d,color);
                if(points[i+5]==1){
                    float hx=points[i+3]*getWidth(),hy=points[i+4]*getHeight(),height=y-hy;
                    if(height>3*d){
                        float half=height*.24f,left=Math.min(x,hx)-half,right=Math.max(x,hx)+half,top=hy-3*d,bottom=y+2*d;
                        if(box){stroke(0xbb101216,3*d);c.drawRoundRect(left,top,right,bottom,2*d,2*d,paint);stroke(color,1.3f*d);c.drawRoundRect(left,top,right,bottom,2*d,2*d,paint);}if(life)healthBar(c,row,left,top,bottom,d);
                        if(name && row<names.length)label(c,names[row],hx,hy-9*d,d,color);
                    }
                }
            }
            if(distance)label(c,"~"+Math.round(points[i+2])+" m",x,y+15*d,d,color);
        }
        c.restore();
        if(stride==33&&motion.animating(SystemClock.uptimeMillis()))postInvalidateOnAnimation();
    }
}
