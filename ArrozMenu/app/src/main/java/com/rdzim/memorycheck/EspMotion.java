package com.rdzim.memorycheck;
import java.util.HashMap;

/** Short interpolation only between validated samples of the same entity. No extrapolation. */
final class EspMotion {
    static final long TTL_MS=350,BLEND_MS=40;
    private EspFrameCodec.Frame previous;
    private float[] from=new float[0],to=new float[0],display=new float[0];
    private boolean[] blend=new boolean[0];
    private long received,started;
    void clear(){previous=null;from=to=display=new float[0];blend=new boolean[0];received=started=0;}
    void update(EspFrameCodec.Frame next,long arrived,long now){
        float[] old=sample(now).clone();EspFrameCodec.Frame prior=previous;
        boolean fresh=received!=0&&now>=received&&now-received<TTL_MS&&now>=arrived&&now-arrived<TTL_MS;
        to=next.points.clone();from=to.clone();display=to.clone();blend=new boolean[next.names.length];
        if(fresh&&prior!=null){
            HashMap<String,Integer> index=new HashMap<>();for(int i=0;i<prior.ids.length;i++)if(!prior.ids[i].isEmpty())index.put(prior.ids[i],i);
            for(int row=0;row<next.ids.length;row++){
                Integer found=index.get(next.ids[row]);if(found==null)continue;int a=found*33,b=row*33;
                // Discontinuities/teleports snap; never draw a long artificial sweep.
                if(Math.hypot(old[a]-to[b],old[a+1]-to[b+1])>.15)continue;
                blend[row]=true;from[b]=old[a];from[b+1]=old[a+1];from[b+2]=old[a+2];
                for(int k=3;k<33;k+=3)if(old[a+k+2]==1&&to[b+k+2]==1){from[b+k]=old[a+k];from[b+k+1]=old[a+k+1];}
            }
        }
        previous=next;received=arrived;started=now;
    }
    float[] sample(long now){
        if(to.length==0)return to;float t=Math.max(0,Math.min(1,(now-started)/(float)BLEND_MS));System.arraycopy(to,0,display,0,to.length);
        if(t<1)for(int row=0;row<blend.length;row++)if(blend[row]){int b=row*33;for(int k=0;k<33;k++)if(k<3||(k-2)%3!=0)display[b+k]=from[b+k]+(to[b+k]-from[b+k])*t;}
        return display;
    }
    boolean animating(long now){if(now<started||now-started>=BLEND_MS)return false;for(boolean b:blend)if(b)return true;return false;}
}
