package com.jsruntimehost.unittests;
import org.java_websocket.client.WebSocketClient;
import org.java_websocket.handshake.ServerHandshake;
import java.net.URI;
import java.net.URISyntaxException;

public class WebSocket extends WebSocketClient {
        public WebSocket(String url) throws URISyntaxException {
                super(new URI(url));
                System.out.println("socket init.");

        }

        @Override
        public void onOpen(ServerHandshake handshakedata)
        {
                System.out.println("Socket opened.");
                this.openCallback();
        }

        @Override
        public void onMessage(String message) {
                System.out.println("received message: " + message);
                this.messageCallback(message);

        }

        @Override
        public void onClose(int code, String reason, boolean remote)
        {

                System.out.println("closed with exit code " + code + " additional info: " + reason);
                this.closeCallback();

        }

        @Override
        public void onError(Exception ex) {
        System.err.println("an error occurred:" + ex);
        }

        public native void openCallback();
        public native void closeCallback();
        public native void messageCallback(String message);
        public native void errorCallback();
}