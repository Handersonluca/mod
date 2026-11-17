package com.rdzim.memorycheck;
public final class EspWireInteropTest {
 public static void main(String[] args)throws Exception{
  String line=new java.io.BufferedReader(new java.io.InputStreamReader(System.in,"UTF-8")).readLine();EspFrameCodec.Frame f=EspFrameCodec.parse(line);
  if(f.points.length!=66||!f.ids[0].equals("abc")||!f.ids[1].equals("def")||!f.names[0].equals("Lucas")||f.health[0]!=75||f.health[1]!=200||f.health[2]!=-1||!f.nearValid||f.unknown!=1||Math.abs(f.points[33]-.75)>.0001)throw new AssertionError("native/Java wire association");
  System.out.println("PASS: real native FRAME4 decoded by Java, per-entity name/HP/position identity and quaternion orientation");
 }
}
