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
        assertFalse(IPUtil.isValidIPv4Address(""));
        assertFalse(IPUtil.isValidIPv4Address("127.0.0.1"));
        // 1 <= port <= 65535
        assertFalse(IPUtil.isValidIPv4Address("127.0.0.1:"));
        assertFalse(IPUtil.isValidIPv4Address("127.0.0.1:0"));
        assertTrue(IPUtil.isValidIPv4Address("127.0.0.1:1"));
        assertTrue(IPUtil.isValidIPv4Address("127.0.0.1:65535"));
        assertFalse(IPUtil.isValidIPv4Address("127.0.0.1:65536"));
        assertFalse(IPUtil.isValidIPv4Address("127:8000"));
        assertFalse(IPUtil.isValidIPv4Address("127.0:8000"));
        assertFalse(IPUtil.isValidIPv4Address("127.0.0.0.1:8000"));
        assertTrue(IPUtil.isValidIPv4Address("127.255.0.1:8000"));
        assertFalse(IPUtil.isValidIPv4Address("127.256.0.1:8000"));
        assertFalse(IPUtil.isValidIPv4Address("www.example.com:8000"));
    }
}