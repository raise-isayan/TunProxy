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
    public void testIsValidPostPort() {
        assertFalse(IPUtil.isValidHostPort(""));
        assertFalse(IPUtil.isValidHostPort("127.0.0.1"));
        // 1 <= port <= 65535
        assertFalse(IPUtil.isValidHostPort("127.0.0.1:"));
        assertFalse(IPUtil.isValidHostPort("127.0.0.1:0"));
        assertTrue(IPUtil.isValidHostPort("127.0.0.1:1"));
        assertTrue(IPUtil.isValidHostPort("127.0.0.1:65535"));
        assertFalse(IPUtil.isValidHostPort("127.0.0.1:65536"));
        assertFalse(IPUtil.isValidHostPort("127:8000"));
        assertFalse(IPUtil.isValidHostPort("127.0:8000"));
        assertFalse(IPUtil.isValidHostPort("127.0.0.0.1:8000"));
        assertTrue(IPUtil.isValidHostPort("127.255.0.1:8000"));
        assertFalse(IPUtil.isValidHostPort("127.256.0.1:8000"));
        assertTrue(IPUtil.isValidHostPort("www.example.com:8000"));
        assertTrue(IPUtil.isValidHostPort("localhost:8080"));
    }

    @Test
    public void testIsDomain() {
        assertFalse(IPUtil.isDomain(""));
        assertFalse(IPUtil.isDomain("127"));
        assertTrue(IPUtil.isDomain("localhost"));
        assertTrue(IPUtil.isDomain("www.example.com"));
        assertTrue(IPUtil.isDomain("xn--eckwd4c7cu47r2wf.jp"));
    }

    @Test
    public void testIsValidIPv4Address() {
        assertFalse(IPUtil.isValidIPv4Address(""));
        assertTrue(IPUtil.isValidIPv4Address("127.0.0.1"));
        assertFalse(IPUtil.isValidIPv4Address("127.0.0.1:"));
        assertFalse(IPUtil.isValidIPv4Address("127.0.0.1:0"));
        assertFalse(IPUtil.isValidIPv4Address("127.0.0.1:1"));
        assertFalse(IPUtil.isValidIPv4Address("127.0.0.1:65535"));
        assertTrue(IPUtil.isValidIPv4Address("192.168.2.11"));
        assertFalse(IPUtil.isValidIPv4Address("192.168.2.256"));
        assertFalse(IPUtil.isValidIPv4Address("192.168.256.11"));
        assertFalse(IPUtil.isValidIPv4Address("192.256.2.11"));
        assertFalse(IPUtil.isValidHostPort("www.example.com"));
    }
}