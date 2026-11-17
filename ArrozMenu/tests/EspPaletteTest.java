package com.rdzim.memorycheck;
public final class EspPaletteTest {
    static void check(boolean x){if(!x)throw new AssertionError();}
    public static void main(String[] args){
        check(EspPalette.color(0)==0xffff0000);check(EspPalette.color(60)==0xffffff00);check(EspPalette.color(120)==0xff00ff00);check(EspPalette.color(180)==0xff00ffff);check(EspPalette.color(240)==0xff0000ff);check(EspPalette.color(300)==0xffff00ff);check(EspPalette.color(360)==0xffff0000);check(EspPalette.color(420)==0xffffffff);
        check(EspPalette.color(-1)==EspPalette.color(0));check(EspPalette.color(999)==EspPalette.color(420));
        for(int i=0;i<=EspPalette.MAX;i++){int color=EspPalette.color(i);check((color>>>24)==255);check(EspPalette.color(EspPalette.position(color))==color);if(i>0){int previous=EspPalette.color(i-1);for(int shift:new int[]{0,8,16})check(Math.abs(((color>>>shift)&255)-((previous>>>shift)&255))<=5);}}
        System.out.println("PASS: full spectrum, white endpoint, bounds, reverse mapping and smooth steps");
    }
}
