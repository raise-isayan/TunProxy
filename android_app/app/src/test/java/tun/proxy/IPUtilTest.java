package tun.proxy;

import org.junit.Test;

import tun.utils.NetUtil;

import static org.junit.Assert.*;

/**
 * Example local unit test, which will execute on the development machine (host).
 *
 * @see <a href="http://d.android.com/tools/testing">Testing documentation</a>
 */
public class IPUtilTest {

    @Test
    public void testIsValidPostPort() {
        assertFalse(NetUtil.isValidHostPort(""));
        assertFalse(NetUtil.isValidHostPort("127.0.0.1"));
        // 1 <= port <= 65535
        assertFalse(NetUtil.isValidHostPort("127.0.0.1:"));
        assertTrue(NetUtil.isValidHostPort("127.0.0.1:0"));
        assertTrue(NetUtil.isValidHostPort("127.0.0.1:1"));
        assertTrue(NetUtil.isValidHostPort("127.0.0.1:65535"));
        assertFalse(NetUtil.isValidHostPort("127.0.0.1:65536"));
        assertFalse(NetUtil.isValidHostPort("127:8000"));
        assertFalse(NetUtil.isValidHostPort("127.0:8000"));
        assertFalse(NetUtil.isValidHostPort("127.0.0.0.1:8000"));
        assertTrue(NetUtil.isValidHostPort("127.255.0.1:8000"));
        assertFalse(NetUtil.isValidHostPort("127.256.0.1:8000"));
        assertTrue(NetUtil.isValidHostPort("www.example.com:8000"));
        assertTrue(NetUtil.isValidHostPort("localhost:8080"));
    }

    @Test
    public void testIsDomain() {
        assertFalse(NetUtil.isDomain(""));
        assertFalse(NetUtil.isDomain("127"));
        assertTrue(NetUtil.isDomain("localhost"));
        assertTrue(NetUtil.isDomain("www.example.com"));
        assertTrue(NetUtil.isDomain("xn--eckwd4c7cu47r2wf.jp"));
    }

    @Test
    public void testIsValidIPv4Address() {
        assertFalse(NetUtil.isValidIPv4Address(""));
        assertTrue(NetUtil.isValidIPv4Address("127.0.0.1"));
        assertFalse(NetUtil.isValidIPv4Address("127.0.0.1:"));
        assertFalse(NetUtil.isValidIPv4Address("127.0.0.1:0"));
        assertFalse(NetUtil.isValidIPv4Address("127.0.0.1:1"));
        assertFalse(NetUtil.isValidIPv4Address("127.0.0.1:65535"));
        assertTrue(NetUtil.isValidIPv4Address("192.168.2.11"));
        assertFalse(NetUtil.isValidIPv4Address("192.168.2.256"));
        assertFalse(NetUtil.isValidIPv4Address("192.168.256.11"));
        assertFalse(NetUtil.isValidIPv4Address("192.256.2.11"));
        assertFalse(NetUtil.isValidHostPort("www.example.com"));
    }
}