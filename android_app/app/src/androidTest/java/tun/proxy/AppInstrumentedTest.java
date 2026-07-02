package tun.proxy;

import android.content.Context;

import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.List;

import tun.utils.Util;

import static org.junit.Assert.*;

/**
 * Instrumented test, which will execute on an Android device.
 *
 * @see <a href="http://d.android.com/tools/testing">Testing documentation</a>
 */
@RunWith(AndroidJUnit4.class)
public class AppInstrumentedTest {

    @Before
    public void setUp() {
        System.loadLibrary("tun2http");
    }

    @After
    public void tearDown() {

    }

    @Test
    public void useAppContext() {
        // Context of the app under test.
        Context appContext = InstrumentationRegistry.getInstrumentation().getTargetContext();
        assertEquals("tun.proxy", appContext.getPackageName());

        List<String> dnsList = Util.getDefaultDNS(appContext);
        System.out.println("dnsList:" + dnsList.size());
        for (String dns : dnsList) {
            System.out.println("dns:" + dns);
        }

    }
}
