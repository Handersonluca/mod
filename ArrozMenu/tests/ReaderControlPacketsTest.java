package com.rdzim.memorycheck;
public final class ReaderControlPacketsTest {
    public static void main(String[] args){
        ReaderControlPackets packets=new ReaderControlPackets();
        if(packets.prepare(0,5,130,true).text.startsWith("ARM 0"))throw new AssertionError("spurious reset");
        packets.disarm();ReaderControlPackets.Packet rapid=packets.prepare(2,15,360,true);if(!rapid.text.startsWith("ARM 0\nTARGET 2\n")||!rapid.text.endsWith("ARM 1\n"))throw new AssertionError("off/on lost");
        if(packets.prepare(2,15,360,true).text.startsWith("ARM 0"))throw new AssertionError("old reset replay");
        packets.failed(rapid);if(!packets.prepare(0,0,0,true).text.startsWith("ARM 0\n"))throw new AssertionError("failed disarm lost");
        packets.disarm();ReaderControlPackets.Packet off=packets.prepare(0,0,0,false);if(!off.text.endsWith("ARM 0\n")||off.text.endsWith("ARM 1\n"))throw new AssertionError("off packet");
        packets.disarm();packets.failed(off);if(!packets.prepare(0,0,0,true).text.startsWith("ARM 0\n"))throw new AssertionError("newer disarm lost");
        System.out.println("PASS: explicit disarm survives off/on coalescing and failed sends; ordinary updates do not rearm faults");
    }
}
