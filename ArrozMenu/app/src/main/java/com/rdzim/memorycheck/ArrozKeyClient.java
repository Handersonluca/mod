package com.rdzim.memorycheck;

import org.json.JSONObject;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.security.Signature;
import java.util.Locale;
import javax.net.ssl.HttpsURLConnection;
import net.i2p.crypto.eddsa.EdDSAEngine;
import net.i2p.crypto.eddsa.EdDSAPublicKey;
import net.i2p.crypto.eddsa.spec.EdDSANamedCurveTable;
import net.i2p.crypto.eddsa.spec.EdDSAPublicKeySpec;

final class ArrozKeyClient {
    static final class AuthException extends Exception {
        AuthException(String message) { super(message); }
    }
    static final class TransportTimeout extends java.net.SocketTimeoutException {
        final String phase;
        TransportTimeout(String phase, java.net.SocketTimeoutException cause) {
            super("License validation timeout");
            this.phase = phase;
            initCause(cause);
        }
    }
    static final class Grant {
        final long expirySeconds, remainingSeconds, bindingVersion;
        Grant(long expirySeconds, long remainingSeconds, long bindingVersion) {
            this.expirySeconds = expirySeconds;
            this.remainingSeconds = remainingSeconds;
            this.bindingVersion = bindingVersion;
        }
    }
    static final class Reply {
        final JSONObject json;
        final long serverSeconds;
        Reply(JSONObject json, long serverSeconds) {
            this.json = json;
            this.serverSeconds = serverSeconds;
        }
    }
    static final class Validation {
        final String key, nonce, device;
        final boolean checking;
        final long version;
        Validation(String key, String nonce, String device, boolean checking, long version) {
            this.key = key; this.nonce = nonce; this.device = device;
            this.checking = checking; this.version = version;
        }
        JSONObject json() throws org.json.JSONException {
            return new JSONObject().put("key", key).put("nonce", nonce).put("device", device)
                    .put("mode", checking ? "check" : "login").put("binding_version", version);
        }
    }
    interface Transport { Reply post(Validation request) throws Exception; }
    interface Progress { void show(String message); }

    private static final SecureRandom RANDOM = new SecureRandom();
    private final Transport transport;
    private final Progress progress;
    private String activeKey = "";
    private String deviceHash = "";
    private long bindingVersion;

    ArrozKeyClient() { this(new HttpsTransport()); }
    ArrozKeyClient(Transport transport) { this(transport, value -> {}); }
    private ArrozKeyClient(Transport transport, Progress progress) {
        this.transport = transport;
        this.progress = progress;
    }
    static ArrozKeyClient withProgress(Progress progress) {
        return new ArrozKeyClient(new HttpsTransport(), progress);
    }

    synchronized Grant license(String key, String device) throws Exception {
        activeKey = "";
        if (key == null) throw new AuthException("Digite sua chave de acesso.");
        String normalized = key.trim().toUpperCase(Locale.ROOT);
        if (!normalized.matches("[A-Z0-9_-]{1,128}")) {
            throw new AuthException("Digite uma chave com letras, números, hífen ou sublinhado.");
        }
        if (device == null || !device.matches("[a-f0-9]{64}")) {
            throw new AuthException("Identificação do aparelho indisponível.");
        }
        deviceHash = device;
        Grant grant = request(normalized, "LICENSE", false);
        bindingVersion = grant.bindingVersion;
        activeKey = normalized;
        return grant;
    }

    synchronized Grant check() throws Exception {
        if (activeKey.isEmpty()) throw new AuthException("Entre novamente com sua chave.");
        return request(activeKey, "CHECK", true);
    }

    private Grant request(String key, String stage, boolean checking) throws Exception {
        try {
            Reply reply = null;
            String nonce = nonce();
            for (int attempt = 1; attempt <= 2; attempt++) {
                progress.show("Consultando a Central de Chaves… tentativa " + attempt + " de 2");
                try {
                    reply = transport.post(new Validation(key, nonce, deviceHash, checking, bindingVersion));
                    break;
                } catch (java.net.SocketTimeoutException error) {
                    if (attempt == 2) throw error;
                    Thread.sleep(1000);
                }
            }
            if (!nonce.equals(reply.json.optString("nonce", ""))) {
                throw new AuthException("Resposta de outra solicitação. Tente novamente.");
            }
            if (!Boolean.TRUE.equals(reply.json.opt("valid"))) {
                String reason = reply.json.optString("status", "");
                if ("outro_aparelho".equals(reason)) throw new AuthException("Esta chave está vinculada a outro aparelho. Solicite o reset no painel.");
                if ("sessao_resetada".equals(reason)) throw new AuthException("O vínculo desta chave foi resetado. Entre novamente.");
                throw new AuthException("Chave inválida, expirada ou bloqueada.");
            }
            Object raw = reply.json.get("expires_at");
            if (!(raw instanceof Number)) throw new AuthException("Validade inválida na resposta.");
            Number expiry = (Number) raw;
            long expires = expiry.longValue();
            if (expiry.doubleValue() != (double) expires || expires <= reply.serverSeconds ||
                    expires > 999999999999L) {
                throw new AuthException("Chave expirada ou validade inválida.");
            }
            Object rawVersion = reply.json.get("binding_version");
            if (!(rawVersion instanceof Number)) throw new AuthException("Vínculo do aparelho inválido.");
            Number version = (Number) rawVersion;
            long revision = version.longValue();
            if (revision < 0 || revision > 9007199254740991L || version.doubleValue() != (double) revision ||
                    (checking && revision != bindingVersion)) throw new AuthException("Vínculo do aparelho inválido.");
            return new Grant(expires, expires - reply.serverSeconds, revision);
        } catch (Exception error) {
            throw new AuthFailure(stage, error);
        }
    }

    private static String nonce() {
        byte[] bytes = new byte[16];
        RANDOM.nextBytes(bytes);
        StringBuilder result = new StringBuilder(32);
        for (byte value : bytes) result.append(String.format(Locale.ROOT, "%02x", value & 255));
        return result.toString();
    }

    static Reply verify(byte[] body, String signatureHex, String stamp, String publicKey,
                        long nowSeconds) throws Exception {
        if (body.length > 16384 || stamp == null || !stamp.matches("[0-9]{1,12}")) {
            throw new AuthException("Resposta de autenticação inválida.");
        }
        long time = Long.parseLong(stamp);
        if (time < nowSeconds - 30 || time > nowSeconds + 30) {
            throw new AuthException("Confira a data e hora automáticas do aparelho.");
        }
        Signature verifier = new EdDSAEngine();
        verifier.initVerify(new EdDSAPublicKey(new EdDSAPublicKeySpec(
                hex(publicKey, 32), EdDSANamedCurveTable.getByName("Ed25519"))));
        verifier.update(stamp.getBytes(StandardCharsets.UTF_8));
        verifier.update(body);
        if (!verifier.verify(hex(signatureHex, 64))) {
            throw new AuthException("Assinatura da resposta inválida.");
        }
        return new Reply(new JSONObject(new String(body, StandardCharsets.UTF_8)), time);
    }

    private static byte[] hex(String value, int size) throws AuthException {
        if (value == null || value.length() != size * 2) throw new AuthException("Assinatura ausente.");
        byte[] result = new byte[size];
        for (int i = 0; i < size; i++) {
            int a = Character.digit(value.charAt(i * 2), 16);
            int b = Character.digit(value.charAt(i * 2 + 1), 16);
            if (a < 0 || b < 0) throw new AuthException("Assinatura inválida.");
            result[i] = (byte) ((a << 4) | b);
        }
        return result;
    }

    static final class HttpsTransport implements Transport {
        public Reply post(Validation request) throws Exception {
            HttpsURLConnection connection = (HttpsURLConnection) new URL(ArrozKeyConfig.ENDPOINT).openConnection();
            connection.setConnectTimeout(20000);
            connection.setReadTimeout(30000);
            connection.setInstanceFollowRedirects(false);
            connection.setRequestMethod("POST");
            connection.setDoOutput(true);
            connection.setUseCaches(false);
            connection.setRequestProperty("Content-Type", "application/json; charset=UTF-8");
            connection.setRequestProperty("Accept", "application/json");
            connection.setRequestProperty("User-Agent", "ArrozMenu/3.1.7 (Android)");
            byte[] payload = request.json().toString().getBytes(StandardCharsets.UTF_8);
            connection.setFixedLengthStreamingMode(payload.length);
            String phase = "CONNECT_SEND";
            try {
                try (OutputStream out = connection.getOutputStream()) { out.write(payload); }
                phase = "RESPONSE";
                int status = connection.getResponseCode();
                if (status == 426) throw new AuthException("Atualize o Arroz Menu para usar o vínculo por aparelho.");
                if (status == 429) throw new AuthException("Muitas tentativas. Aguarde um pouco.");
                if (status != 200 && status != 403) {
                    throw new AuthException("Central de Chaves indisponível (" + status + "). Tente novamente.");
                }
                phase = "BODY";
                InputStream stream = status == 200 ? connection.getInputStream() : connection.getErrorStream();
                if (stream == null) throw new AuthException("Resposta de autenticação ausente.");
                ByteArrayOutputStream data = new ByteArrayOutputStream();
                try (InputStream in = stream) {
                    byte[] chunk = new byte[2048];
                    int count;
                    while ((count = in.read(chunk)) != -1) {
                        if (data.size() + count > 16384) throw new AuthException("Resposta muito grande.");
                        data.write(chunk, 0, count);
                    }
                }
                Reply reply = verify(data.toByteArray(), connection.getHeaderField("x-signature-ed25519"),
                        connection.getHeaderField("x-signature-timestamp"), ArrozKeyConfig.PUBLIC_KEY,
                        System.currentTimeMillis() / 1000);
                if (status != 200 && Boolean.TRUE.equals(reply.json.opt("valid"))) {
                    throw new AuthException("Resposta de autenticação inconsistente.");
                }
                return reply;
            } catch (java.net.SocketTimeoutException error) {
                throw new TransportTimeout(phase, error);
            } finally {
                connection.disconnect();
            }
        }
    }
}
