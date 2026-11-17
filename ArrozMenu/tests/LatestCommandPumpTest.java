package com.rdzim.memorycheck;
import java.util.*;
import java.util.concurrent.Executor;
public final class LatestCommandPumpTest {
    static final class Queue implements Executor {final ArrayDeque<Runnable> jobs=new ArrayDeque<>();public void execute(Runnable r){jobs.add(r);}void next(){jobs.remove().run();}}
    public static void main(String[] args){
        Queue queue=new Queue();int[] value={0},seen={0},writes={0};LatestCommandPump pump=new LatestCommandPump(queue,()->{seen[0]=value[0];writes[0]++;});
        for(int i=1;i<=10000;i++){value[0]=i;pump.request();}if(queue.jobs.size()!=1)throw new AssertionError("unbounded queue");queue.next();if(writes[0]!=1||seen[0]!=10000||!queue.jobs.isEmpty())throw new AssertionError("stale state");
        LatestCommandPump[] holder=new LatestCommandPump[1];holder[0]=new LatestCommandPump(queue,()->{writes[0]++;if(writes[0]==2)for(int i=0;i<100;i++)holder[0].request();});holder[0].request();queue.next();if(queue.jobs.size()!=1)throw new AssertionError("follow-up coalescing");queue.next();if(writes[0]!=3||!queue.jobs.isEmpty())throw new AssertionError();
        pump.request();pump.close();queue.next();if(writes[0]!=3)throw new AssertionError("write after close");pump.request();if(!queue.jobs.isEmpty())throw new AssertionError("closed queue");
        LatestCommandPump rejected=new LatestCommandPump(r->{throw new java.util.concurrent.RejectedExecutionException();},()->{throw new AssertionError();});rejected.request();
        System.out.println("PASS: 10000 updates coalesced, current state, updates during send, closed/rejected execution");
    }
}
