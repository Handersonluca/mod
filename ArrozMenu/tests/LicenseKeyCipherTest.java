package com.rdzim.memorycheck;
import javax.crypto.SecretKey;
import javax.crypto.spec.SecretKeySpec;
import java.security.GeneralSecurityException;
import java.util.Base64;
public final class LicenseKeyCipherTest {
    static void check(boolean x){if(!x)throw new AssertionError();}
    static void reject(SecretKey key,String blob)throws Exception{try{LicenseKeyCipher.decrypt(key,blob);throw new AssertionError("tamper accepted");}catch(GeneralSecurityException expected){}}
    public static void main(String[] args)throws Exception{
        byte[] data=new byte[32];new java.security.SecureRandom().nextBytes(data);SecretKey key=new SecretKeySpec(data,"AES");String value="EXAMPLE-KEY-ação-🔑";
        String a=LicenseKeyCipher.encrypt(key,value),b=LicenseKeyCipher.encrypt(key,value);check(!a.equals(b));check(LicenseKeyCipher.decrypt(key,a).equals(value));check(!a.contains(value));
        byte[] raw=Base64.getDecoder().decode(a);raw[raw.length-1]^=1;reject(key,Base64.getEncoder().encodeToString(raw));
        raw=Base64.getDecoder().decode(a);raw[0]=2;reject(key,Base64.getEncoder().encodeToString(raw));
        raw=Base64.getDecoder().decode(a);raw[1]=11;reject(key,Base64.getEncoder().encodeToString(raw));
        reject(new SecretKeySpec(new byte[32],"AES"),a);reject(key,"not-base64!");reject(key,"");
        try{LicenseKeyCipher.encrypt(key,"");throw new AssertionError();}catch(GeneralSecurityException expected){}
        System.out.println("PASS: encrypted key round-trip, Unicode, random IV, tamper/wrong-key/malformed rejection");
    }
}
