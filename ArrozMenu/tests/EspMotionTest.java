package com.rdzim.memorycheck;
public final class EspMotionTest {
    static void near(float actual,float expected){if(Math.abs(actual-expected)>.00001)throw new AssertionError(actual+" != "+expected);}
    static EspFrameCodec.Frame frame(String[] ids,float... x){float[] points=new float[x.length*33];for(int i=0;i<x.length;i++){points[i*33]=x[i];points[i*33+1]=.5f;points[i*33+2]=10;points[i*33+3]=x[i];points[i*33+4]=.4f;points[i*33+5]=1;}String[] names=new String[x.length];java.util.Arrays.fill(names,"");return new EspFrameCodec.Frame(points,new float[x.length*2],names,ids,0,0,false);}
    public static void main(String[] args){
        EspMotion m=new EspMotion();m.update(frame(new String[]{"a","b"},.2f,.4f),1000,1000);near(m.sample(1000)[0],.2f);
        m.update(frame(new String[]{"b","a"},.42f,.24f),1033,1033);near(m.sample(1033)[0],.4f);near(m.sample(1053)[0],.41f);near(m.sample(1053)[33],.22f);near(m.sample(1073)[0],.42f);if(m.animating(1073))throw new AssertionError("blend did not finish");
        if(m.sample(1053)[5]!=1)throw new AssertionError("flag interpolated");
        m.update(frame(new String[]{"c"},.7f),1100,1100);near(m.sample(1100)[0],.7f);
        m.update(frame(new String[]{"c"},.2f),1133,1133);near(m.sample(1133)[0],.2f);
        m.update(frame(new String[]{"c"},.24f),1600,1600);near(m.sample(1600)[0],.24f);
        m.update(frame(new String[]{""},.25f),1633,1633);near(m.sample(1633)[0],.25f);
        m.update(frame(new String[]{}),1666,1666);if(m.sample(1666).length!=0)throw new AssertionError("empty frame retained targets");
        m.clear();if(m.sample(1700).length!=0)throw new AssertionError("clear retained targets");
        System.out.println("PASS: stable IDs across reordered players, bounded interpolation, flags, new/teleported/stale/legacy frames and explicit clear");
    }
}
