package com.rdzim.memorycheck;

import javax.crypto.Cipher;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;
import java.nio.ByteBuffer;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.util.Base64;

/** Authenticated encryption only; remembering a key never creates an access grant. */
final class LicenseKeyCipher {
    private static final byte[] AAD="LucasMenu/saved-license/v1".getBytes(StandardCharsets.UTF_8);
    static String encrypt(SecretKey key,String value)throws GeneralSecurityException {
        if(value==null||value.isEmpty()||value.length()>256)throw new GeneralSecurityException("Chave inválida");
        Cipher cipher=Cipher.getInstance("AES/GCM/NoPadding");cipher.init(Cipher.ENCRYPT_MODE,key);cipher.updateAAD(AAD);
        byte[] iv=cipher.getIV();if(iv==null||iv.length!=12)throw new GeneralSecurityException("IV inválido");
        byte[] encrypted=cipher.doFinal(value.getBytes(StandardCharsets.UTF_8));
        byte[] packed=ByteBuffer.allocate(2+iv.length+encrypted.length).put((byte)1).put((byte)iv.length).put(iv).put(encrypted).array();
        return Base64.getEncoder().encodeToString(packed);
    }
    static String decrypt(SecretKey key,String saved)throws GeneralSecurityException {
        try{
            if(saved==null||saved.length()>8192)throw new GeneralSecurityException("Formato inválido");
            byte[] bytes=Base64.getDecoder().decode(saved);if(bytes.length<31||bytes[0]!=1||bytes[1]!=12)throw new GeneralSecurityException("Formato inválido");
            byte[] iv=java.util.Arrays.copyOfRange(bytes,2,14),encrypted=java.util.Arrays.copyOfRange(bytes,14,bytes.length);
            Cipher cipher=Cipher.getInstance("AES/GCM/NoPadding");cipher.init(Cipher.DECRYPT_MODE,key,new GCMParameterSpec(128,iv));cipher.updateAAD(AAD);
            String value=StandardCharsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT).onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(cipher.doFinal(encrypted))).toString();
            if(value.isEmpty()||value.length()>256)throw new GeneralSecurityException("Chave inválida");return value;
        }catch(IllegalArgumentException|java.nio.charset.CharacterCodingException e){throw new GeneralSecurityException("Formato inválido",e);}
    }
}
