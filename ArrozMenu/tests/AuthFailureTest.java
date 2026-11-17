package com.rdzim.memorycheck;
public final class AuthFailureTest {
    static void check(Exception cause,String code){String text=AuthFailure.describe(new AuthFailure("INIT",cause));if(!text.contains("INIT/"+code)||text.contains("PRIVATE-KEY"))throw new AssertionError(text);}
    public static void main(String[] args)throws Exception {
        check(new java.net.UnknownHostException("PRIVATE-KEY"),"DNS");
        check(new java.net.SocketTimeoutException("PRIVATE-KEY"),"TIMEOUT");
        check(new javax.net.ssl.SSLHandshakeException("PRIVATE-KEY"),"TLS");
        check(new org.json.JSONException("PRIVATE-KEY"),"FORMAT");
        check(new java.security.SignatureException("PRIVATE-KEY"),"CRYPTO");
        check(new SecurityException("PRIVATE-KEY"),"PERMISSION");
        check(new java.io.IOException("PRIVATE-KEY"),"NETWORK");
        check(new IllegalStateException("PRIVATE-KEY"),"CLIENT");
        String rejected=AuthFailure.describe(new AuthFailure("LICENSE",new ArrozKeyClient.AuthException("Chave recusada")));
        if(!rejected.contains("LICENSE/SERVER")||!rejected.contains("Chave recusada"))throw new AssertionError(rejected);
        ArrozKeyClient broken=new ArrozKeyClient(request->{throw new java.net.UnknownHostException("PRIVATE-KEY");});
        try{broken.license("ARR-01234567-89ABCDEF-01234567", ArrozKeyClientTest.DEVICE);throw new AssertionError("Granted on failure");}catch(AuthFailure e){if(!AuthFailure.describe(e).contains("LICENSE/DNS"))throw new AssertionError();}
        if(LicenseAccess.allowed())throw new AssertionError("Error granted access");
        System.out.println("AUTH_DIAGNOSTIC_PASS: network, TLS, timeout, format, crypto, permission, server; no request details exposed; failure stays locked.");
    }
}
