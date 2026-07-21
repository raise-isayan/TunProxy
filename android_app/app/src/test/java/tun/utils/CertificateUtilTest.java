package tun.utils;

import org.junit.Test;

import static org.junit.Assert.*;

public class CertificateUtilTest {

    @Test
    public void testGetCommonName() {
        assertEquals("Example CA", CertificateUtil.getCommonName("CN=Example CA, O=Example Org, C=US"));
        assertEquals("Example CA", CertificateUtil.getCommonName("CN=Example CA"));
        assertEquals("Example CA", CertificateUtil.getCommonName("O=Example Org, CN=Example CA, C=US"));
        assertEquals("Example CA", CertificateUtil.getCommonName("O=Example Org, C=US, CN=Example CA"));
        assertEquals("", CertificateUtil.getCommonName("O=Example Org, C=US"));
    }

    @Test
    public void testGetOrganization() {
        assertEquals("Example Org", CertificateUtil.getOrganization("CN=Example CA, O=Example Org, C=US"));
        assertEquals("Example Org", CertificateUtil.getOrganization("O=Example Org, CN=Example CA, C=US"));
        assertEquals("Example Org", CertificateUtil.getOrganization("CN=Example CA, C=US, O=Example Org"));
        assertEquals("Example Org", CertificateUtil.getOrganization("O=Example Org"));
        assertEquals("", CertificateUtil.getOrganization("CN=Example CA, C=US"));
    }

    @Test
    public void testEncodeDecode() {
        byte[] original = new byte[]{0x48, 0x65, 0x6c, 0x6c, 0x6f}; // "Hello"
        String encoded = CertificateUtil.encode(original);
        byte[] decoded = CertificateUtil.decode(encoded);
        assertArrayEquals(original, decoded);
    }
}
