package com.rdzim.memorycheck;

/** User-facing diagnostics exclude request bodies, license keys and session IDs. */
final class AuthFailure extends Exception {
    final String stage;
    final Exception problem;
    AuthFailure(String stage,Exception problem){super(problem);this.stage=stage;this.problem=problem;}
    static String describe(Exception failure){
        Exception cause=failure;String stage="DEVICE";
        if(failure instanceof AuthFailure){stage=((AuthFailure)failure).stage;cause=((AuthFailure)failure).problem;}
        String code,message;
        if(cause instanceof ArrozKeyClient.AuthException){code="SERVER";message=cause.getMessage();}
        else if(cause instanceof java.net.UnknownHostException){code="DNS";message="O endereço da Central de Chaves não foi resolvido. Teste outra rede ou o DNS automático.";}
        else if(cause instanceof java.net.SocketTimeoutException){code="TIMEOUT";if(cause instanceof ArrozKeyClient.TransportTimeout)code+="-"+((ArrozKeyClient.TransportTimeout)cause).phase;message="A Central de Chaves demorou para responder. Teste outra rede (Wi-Fi ou dados móveis) e tente novamente.";}
        else if(cause instanceof javax.net.ssl.SSLException){code="TLS";message="Falha na conexão HTTPS. Confira data/hora automáticas e filtros de rede.";}
        else if(cause instanceof org.json.JSONException){code="FORMAT";message="A resposta da Central de Chaves não contém os dados esperados.";}
        else if(cause instanceof java.security.GeneralSecurityException){code="CRYPTO";message="Erro ao verificar a assinatura da resposta.";}
        else if(cause instanceof SecurityException){code="PERMISSION";message="O sistema bloqueou uma operação de autenticação. Confira a permissão de rede do app.";}
        else if(cause instanceof java.io.IOException){code="NETWORK";message="A conexão foi interrompida. Tente novamente em outra rede.";}
        else{code="CLIENT";message="Falha interna na autenticação. Copie este diagnóstico.";}
        return message+"\nCódigo: "+stage+"/"+code+" ("+cause.getClass().getSimpleName()+")";
    }
}
