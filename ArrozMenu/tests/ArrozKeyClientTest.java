package com.rdzim.memorycheck;

import org.json.JSONObject;
import java.nio.charset.StandardCharsets;
import java.security.Signature;
import net.i2p.crypto.eddsa.EdDSAEngine;
import net.i2p.crypto.eddsa.EdDSAPrivateKey;
import net.i2p.crypto.eddsa.spec.EdDSANamedCurveTable;
import net.i2p.crypto.eddsa.spec.EdDSAPrivateKeySpec;

public final class ArrozKeyClientTest {
    static final String KEY = "ARR-01234567-89ABCDEF-01234567";
    static final String DEVICE = new String(new char[64]).replace('\0', 'a');
    interface Action { void run() throws Exception; }
    static void reject(Action action) throws Exception {
        boolean rejected = false;
        try { action.run(); } catch (Exception expected) { rejected = true; }
        if (!rejected) throw new AssertionError("Invalid response accepted");
    }
    static ArrozKeyClient.Reply reply(String nonce) throws Exception {
        return new ArrozKeyClient.Reply(new JSONObject().put("valid", true)
                .put("nonce", nonce).put("expires_at", 2000).put("binding_version", 3), 1000);
    }
    static String hex(byte[] bytes) {
        StringBuilder s = new StringBuilder();
        for (byte b : bytes) s.append(String.format("%02x", b & 255));
        return s.toString();
    }
    public static void main(String[] args) throws Exception {
        final int[] checks = {0};
        ArrozKeyClient client = new ArrozKeyClient(request -> {
            if (!request.key.equals(KEY) || !request.device.equals(DEVICE)) throw new AssertionError();
            if (request.checking) {
                checks[0]++;
                if (request.version != 3) throw new AssertionError("Missing reset revision");
            }
            JSONObject payload = request.json();
            if (!payload.getString("mode").equals(request.checking ? "check" : "login")) throw new AssertionError();
            return reply(request.nonce);
        });
        ArrozKeyClient.Grant grant = client.license(" " + KEY.toLowerCase() + " ", DEVICE);
        if (grant.remainingSeconds != 1000 || grant.expirySeconds != 2000 || grant.bindingVersion != 3) throw new AssertionError();
        client.check();
        if (checks[0] != 1) throw new AssertionError();
        reject(() -> client.license("invalid key", DEVICE));
        reject(client::check);
        reject(() -> client.license(KEY, ""));
        reject(() -> new ArrozKeyClient(request -> reply("wrong")).license(KEY, DEVICE));
        reject(() -> new ArrozKeyClient(request -> new ArrozKeyClient.Reply(
                new JSONObject().put("valid", false).put("nonce", request.nonce), 1000)).license(KEY, DEVICE));
        reject(() -> new ArrozKeyClient(request -> new ArrozKeyClient.Reply(
                new JSONObject().put("valid", true).put("nonce", request.nonce).put("expires_at", 999).put("binding_version", 0), 1000)).license(KEY, DEVICE));
        reject(() -> new ArrozKeyClient(request -> new ArrozKeyClient.Reply(
                new JSONObject().put("valid", true).put("nonce", request.nonce).put("expires_at", 2000.5).put("binding_version", 0), 1000)).license(KEY, DEVICE));
        ArrozKeyClient manual = new ArrozKeyClient(request -> {
            if (!request.key.equals("LUCAS123")) throw new AssertionError();
            return reply(request.nonce);
        });
        manual.license("lucas123", DEVICE);
        manual.check();
        ArrozKeyClient reset = new ArrozKeyClient(request -> {
            ArrozKeyClient.Reply response = reply(request.nonce);
            if (request.checking) response.json.put("binding_version", 4);
            return response;
        });
        reset.license(KEY, DEVICE);
        reject(reset::check);
        byte[] seed = new byte[32];
        new java.security.SecureRandom().nextBytes(seed);
        EdDSAPrivateKeySpec spec = new EdDSAPrivateKeySpec(seed, EdDSANamedCurveTable.getByName("Ed25519"));
        String pub = hex(spec.getA().toByteArray());
        byte[] body = "{\"valid\":true}".getBytes(StandardCharsets.UTF_8);
        Signature signer = new EdDSAEngine();
        signer.initSign(new EdDSAPrivateKey(spec));
        signer.update("1000".getBytes(StandardCharsets.UTF_8));
        signer.update(body);
        String sig = hex(signer.sign());
        ArrozKeyClient.verify(body, sig, "1000", pub, 1000);
        reject(() -> ArrozKeyClient.verify(body, sig, "1000", pub, 1031));
        reject(() -> ArrozKeyClient.verify("{\"valid\":false}".getBytes(StandardCharsets.UTF_8), sig, "1000", pub, 1000));
        reject(() -> ArrozKeyClient.verify(body, null, "1000", pub, 1000));
        System.out.println("ARROZ_DEVICE_PASS: device, login/check mode, reset revision, signature, expiry and nonce.");
    }
}
