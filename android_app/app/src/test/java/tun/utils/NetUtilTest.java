package tun.utils;

import org.junit.Test;

import java.net.InetAddress;
import java.net.UnknownHostException;
import java.util.List;

import static org.junit.Assert.*;

public class NetUtilTest {

    @Test
    public void testIsValidPostPort() {
        assertFalse(NetUtil.isValidHostPort(""));
        assertFalse(NetUtil.isValidHostPort("127.0.0.1"));
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
    public void testIsValiPort() {
        assertTrue(NetUtil.isValiPort(0));
        assertTrue(NetUtil.isValiPort(80));
        assertTrue(NetUtil.isValiPort(65535));
        assertFalse(NetUtil.isValiPort(-1));
        assertFalse(NetUtil.isValiPort(65536));
    }

    @Test
    public void testPlusMinus1() throws UnknownHostException {
        InetAddress addr = InetAddress.getByName("127.0.0.1");
        InetAddress next = NetUtil.plus1(addr);
        assertEquals("127.0.0.2", next.getHostAddress());

        InetAddress prev = NetUtil.minus1(next);
        assertEquals("127.0.0.1", prev.getHostAddress());
    }

    @Test
    public void testToCIDR() throws UnknownHostException {
        List<NetUtil.CIDR> cidrs = NetUtil.toCIDR("192.168.1.0", "192.168.1.255");
        assertEquals(1, cidrs.size());
        assertEquals("192.168.1.0/24", cidrs.get(0).address.getHostAddress() + "/" + cidrs.get(0).prefix);

        cidrs = NetUtil.toCIDR("192.168.1.1", "192.168.1.1");
        assertEquals(1, cidrs.size());
        assertEquals("192.168.1.1/32", cidrs.get(0).address.getHostAddress() + "/" + cidrs.get(0).prefix);
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
        assertFalse(NetUtil.isValidIPv4Address("192"));
        assertFalse(NetUtil.isValidIPv4Address("192.168"));
        assertFalse(NetUtil.isValidIPv4Address("192.168.0"));
        assertFalse(NetUtil.isValidIPv4Address("192.168.0."));
        assertTrue(NetUtil.isValidIPv4Address("127.0.0.1"));
        assertFalse(NetUtil.isValidIPv4Address("127.0.0.1:"));
        assertFalse(NetUtil.isValidIPv4Address("127.0.0.1:0"));
        assertFalse(NetUtil.isValidIPv4Address("127.0.0.1:1"));
        assertFalse(NetUtil.isValidIPv4Address("127.0.0.1:65535"));
        assertTrue(NetUtil.isValidIPv4Address("192.168.2.11"));
        assertFalse(NetUtil.isValidIPv4Address("192.168.2.256"));
        assertFalse(NetUtil.isValidIPv4Address("192.168.256.11"));
        assertFalse(NetUtil.isValidIPv4Address("192.256.2.11"));
        assertTrue(NetUtil.isValidIPv4Address("255.255.255.255"));
        assertFalse(NetUtil.isValidIPv4Address(null));
    }

    @Test
    public void testIsValidIPv6Address() {
        assertFalse(NetUtil.isValidIPv6Address(""));
        assertFalse(NetUtil.isValidIPv6Address(":"));
        assertFalse(NetUtil.isValidIPv6Address("[]"));
        assertTrue(NetUtil.isValidIPv6Address("::"));
        assertTrue(NetUtil.isValidIPv6Address("[::]"));
        assertFalse(NetUtil.isValidIPv6Address(":::"));
        assertTrue(NetUtil.isValidIPv6Address("::1"));
        assertTrue(NetUtil.isValidIPv6Address("[::1]"));
        assertTrue(NetUtil.isValidIPv6Address("2001:0db8:85a3:0000:0000:8a2e:0370:7334"));
        assertTrue(NetUtil.isValidIPv6Address("2001:0db8:85a3:0:0000:8a2e:370:7334"));
        assertTrue(NetUtil.isValidIPv6Address("2001:db8:85a3:0:0:8a2e:370:7334"));
        assertTrue(NetUtil.isValidIPv6Address("2001:db8:85a3::8a2e:370:7334"));
        assertTrue(NetUtil.isValidIPv6Address("2001:db8::1:0:0:1"));
        assertTrue(NetUtil.isValidIPv6Address("2001:0db8:0000:0000:3456::"));
        assertTrue(NetUtil.isValidIPv6Address("2001:0112:0000:0000:0000:0000:0000:0030"));
        assertTrue(NetUtil.isValidIPv6Address("[2001:0112:0000:0000:0000:0000:0000:0030]"));
        assertFalse(NetUtil.isValidIPv6Address("[2001:0112:0000:0000:0000:0000:0000:0030"));
        assertFalse(NetUtil.isValidIPv6Address("2001:0112:0000:0000:0000:0000:0000:0030]"));
        assertFalse(NetUtil.isValidIPv6Address("[[2001:0112:0000:0000:0000:0000:0000:0030]]"));
        assertFalse(NetUtil.isValidIPv6Address("2001:0db8::3456::"));
        assertFalse(NetUtil.isValidIPv6Address("2001:0112::0011::0030"));
        assertFalse(NetUtil.isValidIPv6Address(null));
    }

}
