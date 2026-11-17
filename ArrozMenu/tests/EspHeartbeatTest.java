package com.rdzim.memorycheck;
import java.util.concurrent.*;
public final class EspHeartbeatTest {
 public static void main(String[] args)throws Exception {
  EspSession session=new EspSession(new android.content.Context(),new EspSession.Listener(){public void frame(float[] p,long a){}public void richFrame(EspFrameCodec.Frame f,long a){}public void status(String s,String l){}public void aimStatus(String s){}});
  Process echo=new ProcessBuilder("/bin/cat").start();CountDownLatch arms=new CountDownLatch(3),off=new CountDownLatch(1);
  Thread monitor=new Thread(()->{try(java.io.BufferedReader in=new java.io.BufferedReader(new java.io.InputStreamReader(echo.getInputStream()))){String s;while((s=in.readLine())!=null){if(s.equals("ARM 1"))arms.countDown();if(s.equals("ARM 0"))off.countDown();}}catch(Exception ignored){}});monitor.setDaemon(true);monitor.start();
  try {
   java.lang.reflect.Field process=EspSession.class.getDeclaredField("process");process.setAccessible(true);process.set(session,echo);
   session.setAimEnabled(true);if(!arms.await(1500,TimeUnit.MILLISECONDS))throw new AssertionError("heartbeat relied on blocked UI");
   session.setAimEnabled(false);if(!off.await(1000,TimeUnit.MILLISECONDS))throw new AssertionError("explicit disarm lost");
  }finally{session.close();echo.waitFor(2,TimeUnit.SECONDS);if(echo.isAlive())echo.destroyForcibly();}
  java.lang.reflect.Field field=EspSession.class.getDeclaredField("heartbeat");field.setAccessible(true);if(!((ExecutorService)field.get(session)).isShutdown())throw new AssertionError("heartbeat thread leaked");
  System.out.println("PASS: actual EspSession renews ARM while UI tasks never run, sends disarm and closes heartbeat executor");
 }
}
