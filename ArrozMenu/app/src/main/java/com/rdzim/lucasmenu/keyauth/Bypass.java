package com.rdzim.lucasmenu.keyauth;

public final class Bypass {
    static {
        System.loadLibrary("arroz_jni");
    }

    private Bypass() {}

    public static native void init();

    /**
     * Applies a patch to a private mapping in this process. The caller must
     * keep the region mapped and ensure no other thread is using it.
     */
    public static native boolean applyPatch(long address, byte[] newBytes, byte[] expectedOriginal);

    /** Restores a recorded patch. A failed restore can be retried at the same address. */
    public static native boolean restorePatch(long address);
}
