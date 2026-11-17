package com.rdzim.memorycheck;
import java.util.concurrent.atomic.AtomicBoolean;

/** A disarm edge must survive coalescing a rapid off/on pair. */
final class ReaderControlPackets {
    static final class Packet {final String text;final boolean disarm;Packet(String text,boolean disarm){this.text=text;this.disarm=disarm;}}
    private final AtomicBoolean pendingDisarm=new AtomicBoolean();
    void disarm(){pendingDisarm.set(true);}
    Packet prepare(int target,int visuals,int fov,boolean enabled){
        boolean off=pendingDisarm.getAndSet(false);
        return new Packet((off&&enabled?"ARM 0\n":"")+"TARGET "+target+"\nVIS "+visuals+"\nFOV "+fov+"\n"+(enabled?"ARM 1\n":"ARM 0\n"),off);
    }
    void failed(Packet packet){if(packet.disarm)pendingDisarm.set(true);}
}
