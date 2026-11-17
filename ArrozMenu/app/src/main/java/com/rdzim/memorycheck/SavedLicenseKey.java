package com.rdzim.memorycheck;

import android.content.Context;
import android.content.SharedPreferences;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import java.security.KeyStore;

/** Ciphertext is private app data; the encryption key stays in Android Keystore. */
final class SavedLicenseKey {
    private static final String ALIAS="lucas.menu.saved.license.v1";
    private final SharedPreferences prefs;
    SavedLicenseKey(Context context){prefs=context.getSharedPreferences("saved_license",Context.MODE_PRIVATE);}
    private SecretKey key(boolean create)throws Exception {
        KeyStore store=KeyStore.getInstance("AndroidKeyStore");store.load(null);
        if(store.containsAlias(ALIAS))return (SecretKey)store.getKey(ALIAS,null);
        if(!create)throw new java.security.GeneralSecurityException("Chave local indisponível");
        KeyGenerator generator=KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES,"AndroidKeyStore");
        generator.init(new KeyGenParameterSpec.Builder(ALIAS,KeyProperties.PURPOSE_ENCRYPT|KeyProperties.PURPOSE_DECRYPT)
                .setKeySize(256).setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setRandomizedEncryptionRequired(true).build());
        return generator.generateKey();
    }
    String load()throws Exception {
        String saved=prefs.getString("ciphertext","");if(saved.isEmpty())return "";
        try{return LicenseKeyCipher.decrypt(key(false),saved);}catch(Exception error){clear();throw error;}
    }
    void save(String value)throws Exception {
        String saved=LicenseKeyCipher.encrypt(key(true),value);
        if(!prefs.edit().putString("ciphertext",saved).commit()){clear();throw new java.io.IOException("Gravação indisponível");}
    }
    void clear(){prefs.edit().remove("ciphertext").apply();}
}
