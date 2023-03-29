package com.jsruntimehost;

import android.content.Context;

import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.java_websocket.handshake.ServerHandshake;
import org.junit.Test;
import org.junit.runner.RunWith;
import java.net.URI;
import java.net.URISyntaxException;
import java.util.concurrent.TimeUnit;

import org.java_websocket.client.WebSocketClient;
import org.java_websocket.handshake.ServerHandshake;

import static org.junit.Assert.*;

/**
 * Instrumented test, which will execute on an Android device.
 *
 * @see <a href="http://d.android.com/tools/testing">Testing documentation</a>
 */
@RunWith(AndroidJUnit4.class)
public class Main {
    @Test
    public void javaScriptTests() throws URISyntaxException {
        // Context of the app under test.




        System.out.println("TESTINGGGG.");

        Context appContext = InstrumentationRegistry.getInstrumentation().getTargetContext();
        assertEquals("com.jsruntimehost.unittests", appContext.getPackageName());
        assertEquals(0, Native.javaScriptTests(appContext));

        try
        {
            TimeUnit.SECONDS.sleep(5);
        }  catch (InterruptedException e) {
            e.printStackTrace();
        }
    }
}