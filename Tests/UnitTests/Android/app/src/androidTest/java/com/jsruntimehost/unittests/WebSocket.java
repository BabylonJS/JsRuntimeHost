package com.jsruntimehost.unittests;
import org.java_websocket.client.WebSocketClient;
import org.java_websocket.handshake.ServerHandshake;
import java.net.URI;
import java.net.URISyntaxException;

public class WebSocket {
        public void liyaanMethod() throws URISyntaxException {
                TestWebSocket myTest = new TestWebSocket(new URI("wss://demo.piesocket.com/v3/channel_123?api_key=VCXCEuvhGcBDP7XhiJJUDvR1e1D3eiVjgZ9VRiaV&notify_self"));
                System.out.println("Socket initialized.");
                myTest.connect();
        }
}

class TestWebSocket extends WebSocketClient {
        public TestWebSocket(URI serverUri) {
                super(serverUri);
        }

        @Override
public void onOpen(ServerHandshake handshakedata) {
        System.out.println("Socket opened.");
        send("Hello, it is me :)");
        System.out.println("Message sent.");
        }

@Override
public void onMessage(String message) {
        System.out.println("received message: " + message);
        }

@Override
public void onClose(int code, String reason, boolean remote) {
        System.out.println("closed with exit code " + code + " additional info: " + reason);
        }

@Override
public void onError(Exception ex) {
        System.err.println("an error occurred:" + ex);
        }
}