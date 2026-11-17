package com.rdzim.memorycheck;

import java.util.concurrent.Executor;
import java.util.function.Consumer;

/** Bounded UI delivery: replace pending data instead of replaying old frames. */
public final class LatestFramePump<T> {
    private final Executor executor;
    private final Consumer<T> consumer;
    private T pending;
    private boolean scheduled;

    public LatestFramePump(Executor executor, Consumer<T> consumer) {
        this.executor = executor;
        this.consumer = consumer;
    }

    public synchronized void offer(T value) {
        if (value == null) throw new NullPointerException("frame");
        pending = value;
        if (!scheduled) {
            scheduled = true;
            schedule();
        }
    }

    private void schedule() {
        try { executor.execute(this::deliver); }
        catch (RuntimeException error) { scheduled = false; pending = null; throw error; }
    }

    private void deliver() {
        T value;
        synchronized (this) { value = pending; pending = null; }
        try { consumer.accept(value); }
        finally {
            synchronized (this) {
                if (pending == null) scheduled = false;
                else schedule();
            }
        }
    }
}
