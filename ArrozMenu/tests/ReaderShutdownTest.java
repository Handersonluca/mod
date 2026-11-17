package com.rdzim.memorycheck;
import java.util.concurrent.TimeUnit;
public final class ReaderShutdownTest {
    public static void main(String[] args)throws Exception {
        Process cooperative=new ProcessBuilder("/bin/sh","-c","while IFS= read -r line; do case \"$line\" in STOP) exit 0;; esac; done").start();ReaderShutdown.stop(cooperative);if(!cooperative.waitFor(2,TimeUnit.SECONDS)||cooperative.exitValue()!=0)throw new AssertionError("protocol stop");
        Process stubborn=new ProcessBuilder("/bin/sh","-c","exec sleep 10").start();ReaderShutdown.stop(stubborn);if(!stubborn.waitFor(2,TimeUnit.SECONDS))throw new AssertionError("grace deadline");
        Process blocked=new ProcessBuilder("/bin/sh","-c","exec sleep 10").start();Thread writer=new Thread(()->{try{blocked.getOutputStream().write(new byte[1024*1024]);blocked.getOutputStream().flush();}catch(java.io.IOException expected){}});writer.setDaemon(true);writer.start();Thread.sleep(50);ReaderShutdown.stop(blocked);if(!blocked.waitFor(2,TimeUnit.SECONDS))throw new AssertionError("blocked child");writer.join(2000);if(writer.isAlive())throw new AssertionError("blocked writer remains");
        System.out.println("PASS: cooperative STOP, forced cleanup deadline and blocked-pipe writer release");
    }
}
