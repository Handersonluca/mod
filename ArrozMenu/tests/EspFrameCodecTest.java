package com.rdzim.memorycheck;
import java.nio.charset.StandardCharsets;
public final class EspFrameCodecTest {
    static String row(String metadata){StringBuilder b=new StringBuilder();for(int k=0;k<33;k++)b.append(k==2?" 12.5":" 0");return b+metadata;}
    static String hex(String s){StringBuilder b=new StringBuilder();for(byte x:s.getBytes(StandardCharsets.UTF_8))b.append(String.format("%02x",x&255));return b.toString();}
    static void reject(String s){try{EspFrameCodec.parse(s);throw new AssertionError("accepted malformed frame");}catch(IllegalArgumentException expected){}}
    public static void main(String[] args){
        String a="FRAME3 2 0 0 0"+row(" 75 200 1 "+hex("Pedro 🌟"))+row(" 0 0 0 -");
        EspFrameCodec.Frame f=EspFrameCodec.parse(a);
        if(!f.names[0].equals("Pedro 🌟")||f.health[0]!=75||f.health[1]!=200||f.health[2]!=-1||!f.names[1].isEmpty()||f.points.length!=66)throw new AssertionError("association");
        if(EspFrameCodec.parse("FRAME2 1 0 0 0"+row("")).health[0]!=-1)throw new AssertionError("legacy");
        if(EspFrameCodec.parse("FRAME3 0 0 0 0").points.length!=0)throw new AssertionError("empty");
        reject("FRAME3 129 0 0 0");reject(a+" extra");reject(a.replace(" 75 200 1 "," 201 200 1 "));reject(a.replace(" 75 200 1 "," 75 0 1 "));
        reject("FRAME3 1 0 0 0"+row(" 1 200 0 -"));reject("FRAME3 1 0 0 0"+row(" 0 0 0 c080"));reject("FRAME3 1 0 0 0"+row(" 0 0 0 0a"));reject("FRAME3 1 0 0 0"+row(" 0 0 0 ff"));reject(a.replaceFirst("12.5","NaN"));reject("FRAME3 1 2147483647 2147483647 1"+row(" 0 0 0 -"));
        EspFrameCodec.Frame idFrame=EspFrameCodec.parse("FRAME4 1 0 0 0"+row(" 75 200 1 4c75636173 abc123"));
        if(!idFrame.ids[0].equals("abc123")||!idFrame.names[0].equals("Lucas"))throw new AssertionError("identity frame");
        reject("FRAME4 2 0 0 0"+row(" 0 0 0 - abc")+row(" 0 0 0 - abc"));
        reject("FRAME4 1 0 0 0"+row(" 0 0 0 - 0"));reject("FRAME4 1 0 0 0"+row(" 0 0 0 - ../../"));
        System.out.println("PASS: frame association, UTF-8, health, unknowns, legacy, bounds and malformed rejection");
    }
}
