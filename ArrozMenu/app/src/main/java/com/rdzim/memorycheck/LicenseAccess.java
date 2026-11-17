package com.rdzim.memorycheck;

import android.os.SystemClock;

/** A process-local grant, never a saved boolean or a stored plaintext key. */
final class LicenseAccess {
    private static ArrozKeyClient client;
    private static long expiryElapsed,checkedElapsed,expiresUnix;
    private static int revision;
    static synchronized boolean allowed(){return allowedAt(SystemClock.elapsedRealtime());}
    static synchronized boolean allowedAt(long now){return client!=null&&now<expiryElapsed&&now<checkedElapsed+90000;}
    static synchronized void grant(ArrozKeyClient candidate,ArrozKeyClient.Grant grant){grantAt(candidate,grant,SystemClock.elapsedRealtime());}
    static synchronized void grantAt(ArrozKeyClient candidate,ArrozKeyClient.Grant grant,long now){
        revision++;client=candidate;expiryElapsed=now+Math.min(grant.remainingSeconds,3155760000L)*1000L;
        checkedElapsed=now;expiresUnix=grant.expirySeconds;
    }
    static synchronized long expires(){return expiresUnix;}
    static synchronized void clear(){revision++;client=null;expiryElapsed=0;checkedElapsed=0;expiresUnix=0;}
    static void check()throws Exception {
        ArrozKeyClient candidate;int current;
        synchronized(LicenseAccess.class){if(!allowed())throw new ArrozKeyClient.AuthException("Sessão expirada. Entre novamente.");candidate=client;current=revision;}
        ArrozKeyClient.Grant updated;
        try{updated=candidate.check();}catch(Exception error){synchronized(LicenseAccess.class){if(revision==current)clear();}throw error;}
        synchronized(LicenseAccess.class){if(revision==current&&client==candidate){long now=SystemClock.elapsedRealtime();checkedElapsed=now;expiryElapsed=now+Math.min(updated.remainingSeconds,3155760000L)*1000L;expiresUnix=updated.expirySeconds;}}
    }
    private LicenseAccess(){}
}
