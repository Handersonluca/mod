package com.rdzim.memorycheck;

public final class AuthRetryTest {
    public static void main(String[] args) throws Exception {
        final int[] attempts = {0};
        ArrozKeyClient client = new ArrozKeyClient(request -> {
            if (++attempts[0] == 1) throw new java.net.SocketTimeoutException();
            return ArrozKeyClientTest.reply(request.nonce);
        });
        client.license(ArrozKeyClientTest.KEY, ArrozKeyClientTest.DEVICE);
        if (attempts[0] != 2) throw new AssertionError("Recovery count");

        final int[] failures = {0};
        ArrozKeyClient broken = new ArrozKeyClient(request -> {
            failures[0]++;
            throw new java.net.SocketTimeoutException();
        });
        ArrozKeyClientTest.reject(() -> broken.license(ArrozKeyClientTest.KEY, ArrozKeyClientTest.DEVICE));
        if (failures[0] != 2) throw new AssertionError("Unbounded retry");

        final int[] denials = {0};
        ArrozKeyClient denied = new ArrozKeyClient(request -> {
            denials[0]++;
            return new ArrozKeyClient.Reply(new org.json.JSONObject()
                    .put("valid", false).put("nonce", request.nonce), 1000);
        });
        ArrozKeyClientTest.reject(() -> denied.license(ArrozKeyClientTest.KEY, ArrozKeyClientTest.DEVICE));
        if (denials[0] != 1) throw new AssertionError("Retried denied key");
        System.out.println("ARROZ_RETRY_PASS: at most two attempts, no retry after key denial.");
    }
}
