package com.rdzim.memorycheck;

/** Full hue wheel followed by a red-to-white tail, shared with the slider. */
final class EspPalette {
    static final int MAX=420;
    static int color(int value){
        value=Math.max(0,Math.min(MAX,value));float h=value<=360?value%360:0,s=value<=360?1:(MAX-value)/60f;
        float c=s,x=c*(1-Math.abs((h/60f)%2-1)),m=1-c,r=0,g=0,b=0;
        if(h<60){r=c;g=x;}else if(h<120){r=x;g=c;}else if(h<180){g=c;b=x;}else if(h<240){g=x;b=c;}else if(h<300){r=x;b=c;}else{r=c;b=x;}
        return 0xff000000|(Math.round((r+m)*255)<<16)|(Math.round((g+m)*255)<<8)|Math.round((b+m)*255);
    }
    static int position(int color){
        int selected=0;long best=Long.MAX_VALUE;
        for(int i=0;i<=MAX;i++){int c=color(i);long r=((c>>16)&255)-((color>>16)&255),g=((c>>8)&255)-((color>>8)&255),b=(c&255)-(color&255),distance=r*r+g*g+b*b;if(distance<best){best=distance;selected=i;}}
        return selected;
    }
}
