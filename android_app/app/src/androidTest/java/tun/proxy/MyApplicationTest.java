package tun.proxy;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.util.Log;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;

import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.List;

import tun.utils.ProgressTask;

@RunWith(AndroidJUnit4.class)
public class MyApplicationTest {
    private static final String TAG = "MyApplicationTest";

    @Before
    public void setUp() {

    }

    @After
    public void tearDown() {

    }

    @Test
    public void testMyApplication() {
        MyApplication app = MyApplication.getInstance();
        assertNotNull(app);

        /* VPN setting */

        app.storeVPNMode(MyApplication.VPNMode.DISALLOW);
        assertEquals(MyApplication.VPNMode.DISALLOW, app.loadVPNMode());
        app.storeVPNMode(MyApplication.VPNMode.ALLOW);
        assertEquals(MyApplication.VPNMode.ALLOW, app.loadVPNMode());

        /* Proxy setting */

        app.storeProxyConnectivityCheck(false);
        assertTrue(app.loadProxyConnectivityCheck(true));
        app.storeProxyConnectivityCheck(true);
        assertTrue(app.loadProxyConnectivityCheck(false));

        /* DNS setting */

        app.storeUseDnsCustom(true);
        assertTrue(app.loadUseDnsCustom());
        app.storeUseDnsCustom(false);
        assertFalse(app.loadUseDnsCustom());

        app.storePrimaryDns("1.1.1.1");
        assertEquals("1.1.1.1" , app.loadPrimaryDns(""));
        app.storeSecondaryDns("1.1.1.2");
        assertEquals("1.1.1.2" , app.loadSecondaryDns(""));
    }

    @Test
    public void testProxyType() {
        assertEquals(0, MyApplication.ProxyType.HTTP.ordinal());
        assertEquals(1, MyApplication.ProxyType.SOCKS5.ordinal());
    }
}
