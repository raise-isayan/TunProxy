package tun.proxy;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import org.junit.Test;

import java.util.EnumSet;

public class HostPortPairTest {
    @Test
    public void testHostPortPair() {
        HostPortPair pair = new HostPortPair("127.0.0.1",8080);
        assertEquals("127.0.0.1", pair.getHost());
        assertEquals(8080, pair.getPort());
    }

    @Test
    public void testParse() {
        try {
            HostPortPair pair = HostPortPair.parse("127.0.0.1:8080");
            assertEquals("127.0.0.1", pair.getHost());
            assertEquals(8080, pair.getPort());
        } catch (IllegalArgumentException ex) {
            fail();
        }
        try {
            HostPortPair pair = HostPortPair.parse("www.example.com:80");
            assertEquals("www.example.com", pair.getHost());
            assertEquals(80, pair.getPort());
        } catch (IllegalArgumentException ex) {
            fail();
        }
        try {
            HostPortPair pair = HostPortPair.parse("127.0.0.1:8080:8008");
            fail();
        } catch (IllegalArgumentException ex) {
            assertTrue(true);
        }
        try {
            HostPortPair pair = HostPortPair.parse("127.0.0.1");
            fail();
        } catch (IllegalArgumentException ex) {
            assertTrue(true);
        }
        try {
            HostPortPair pair = HostPortPair.parse("127.0.0.1:");
            fail();
        } catch (IllegalArgumentException ex) {
            assertTrue(true);
        }
    }

    @Test
    public void testValueOf() {
        {
            String hostPort = HostPortPair.valueOf("127.0.0.1",8080);
            assertEquals("127.0.0.1:8080", hostPort);
        }
        {
            String hostPort = HostPortPair.valueOf("192.168.0.1",0);
            assertEquals("192.168.0.1:0", hostPort);
        }
        {
            String hostPort = HostPortPair.valueOf("www.example.com",80);
            assertEquals("www.example.com:80", hostPort);
        }

    }

}