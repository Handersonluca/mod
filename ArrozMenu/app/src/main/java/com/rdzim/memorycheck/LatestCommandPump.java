package com.rdzim.memorycheck;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.atomic.*;

/** Coalesces updates: one command task at a time, carrying current state. */
final class LatestCommandPump {
    private final Executor executor;private final Runnable write;
    private final AtomicBoolean queued=new AtomicBoolean();
    private final AtomicLong revision=new AtomicLong();
    private volatile boolean closed;
    LatestCommandPump(Executor executor,Runnable write){this.executor=executor;this.write=write;}
    void request(){if(closed)return;revision.incrementAndGet();schedule();}
    private void schedule(){
        if(closed||!queued.compareAndSet(false,true))return;
        try{executor.execute(()->{long seen=revision.get();try{if(!closed)write.run();}finally{queued.set(false);if(!closed&&revision.get()!=seen)schedule();}});}
        catch(RejectedExecutionException e){queued.set(false);}
    }
    void close(){closed=true;}
}
