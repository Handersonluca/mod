package com.rdzim.memorycheck;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.RejectedExecutionException;

public final class LatestFramePumpTest {
    static void check(boolean ok,String message){if(!ok)throw new AssertionError(message);}
    public static void main(String[] args){
        ArrayDeque<Runnable> queue=new ArrayDeque<>();
        List<Integer> drawn=new ArrayList<>();
        LatestFramePump<Integer> pump=new LatestFramePump<>(queue::add,drawn::add);
        for(int i=0;i<10000;i++)pump.offer(i);
        check(queue.size()==1,"blocked UI accumulated work");
        queue.remove().run();
        check(drawn.size()==1&&drawn.get(0)==9999,"did not deliver latest");
        pump.offer(0);queue.remove().run();
        check(drawn.get(1)==0,"empty/clear state lost");

        List<Integer> midDraw=new ArrayList<>();
        final LatestFramePump<Integer>[] holder=new LatestFramePump[1];
        holder[0]=new LatestFramePump<>(queue::add,value->{
            midDraw.add(value);
            if(value==1){holder[0].offer(2);holder[0].offer(3);}
        });
        holder[0].offer(1);queue.remove().run();
        check(queue.size()==1,"update while drawing not scheduled once");
        queue.remove().run();
        check(midDraw.toString().equals("[1, 3]"),"update during draw lost");

        boolean[] reject={true};
        LatestFramePump<Integer> retry=new LatestFramePump<>(task->{
            if(reject[0])throw new RejectedExecutionException();queue.add(task);
        },drawn::add);
        try{retry.offer(10);throw new AssertionError("expected rejection");}
        catch(RejectedExecutionException expected){}
        reject[0]=false;retry.offer(11);queue.remove().run();
        check(drawn.get(drawn.size()-1)==11,"failed scheduling wedged pump");
        System.out.println("PASS: 10000 updates bounded, latest delivery, clear, updates during draw, rejected executor recovery");
    }
}
