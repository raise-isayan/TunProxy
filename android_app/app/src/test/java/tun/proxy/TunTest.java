package tun.proxy;

import org.junit.Test;

import tun.utils.IPUtil;

import static org.junit.Assert.*;

/**
 * Example local unit test, which will execute on the development machine (host).
 *
 * @see <a href="http://d.android.com/tools/testing">Testing documentation</a>
 */
public class IPUtilTest {
    @Test
    public void testIsValidIPv4Address() {
        assertEquals(IPUtil.isValidIPv4Address(""), false);
        assertEquals(IPUtil.isValidIPv4Address("127.0.0.1"), true);
        // 1 <= port <= 65535
        assertEquals(IPUtil.isValidIPv4Address("127.0.0.1:0"), false);
        assertEquals(IPUtil.isValidIPv4Address("127.0.0.1:1"), true);
        assertEquals(IPUtil.isValidIPv4Address("127.0.0.1:65535"), true);
        assertEquals(IPUtil.isValidIPv4Address("127.0.0.1:65536"), false);
        assertEquals(IPUtil.isValidIPv4Address("127:8000"), false);
        assertEquals(IPUtil.isValidIPv4Address("127.0:8000"), false);
        assertEquals(IPUtil.isValidIPv4Address("127.0.0.0.1:8000"), false);
        assertEquals(IPUtil.isValidIPv4Address("127.255.0.1:8000"), true);
        assertEquals(IPUtil.isValidIPv4Address("127.256.0.1:8000"), false);
        assertEquals(IPUtil.isValidIPv4Address("www.example.com:8000"), false);
    }
}