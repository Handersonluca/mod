package android.os;
/** Simulate a blocked Android UI: accepted tasks are deliberately never run. */
public final class Handler {public Handler(Looper looper){}public boolean post(Runnable task){return true;}}
