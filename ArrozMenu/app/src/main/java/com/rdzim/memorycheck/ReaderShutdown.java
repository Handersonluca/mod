package com.rdzim.memorycheck;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.TimeUnit;

/** Best-effort protocol stop, bounded grace period, then close every pipe. */
final class ReaderShutdown {
    static void stop(Process target){
        Thread graceful=new Thread(()->{try{target.getOutputStream().write("ARM 0\nSTOP\n".getBytes(StandardCharsets.US_ASCII));target.getOutputStream().flush();target.getOutputStream().close();}catch(Exception ignored){}},"esp-stop-command");graceful.setDaemon(true);graceful.start();
        Thread cleanup=new Thread(()->{
            try{if(!target.waitFor(300,TimeUnit.MILLISECONDS))target.destroyForcibly();}
            catch(InterruptedException e){Thread.currentThread().interrupt();target.destroyForcibly();}
            finally{try{target.getInputStream().close();}catch(Exception ignored){}try{target.getErrorStream().close();}catch(Exception ignored){}try{target.getOutputStream().close();}catch(Exception ignored){}}
        },"esp-stop-cleanup");cleanup.setDaemon(true);cleanup.start();
    }
}
