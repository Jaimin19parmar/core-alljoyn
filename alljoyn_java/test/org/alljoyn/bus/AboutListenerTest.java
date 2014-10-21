/*
 * Copyright (c) 2014 AllSeen Alliance. All rights reserved.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
package org.alljoyn.bus;

import java.util.HashMap;
import java.util.Map;

import junit.framework.TestCase;

import org.alljoyn.bus.BusAttachment.RemoteMessage;

public class AboutListenerTest  extends TestCase {
    static {
        System.loadLibrary("alljoyn_java");
    }

    private BusAttachment serviceBus;
    static short PORT_NUMBER = 542;

    public synchronized void stopWait() {
        this.notifyAll();
    }

    public void setUp() throws Exception {
        serviceBus = new BusAttachment("AboutListenerTestService");

        assertEquals(Status.OK, serviceBus.connect());
        AboutListenerTestSessionPortListener listener = new AboutListenerTestSessionPortListener();
        short contactPort = PORT_NUMBER;
        Mutable.ShortValue sessionPort = new Mutable.ShortValue(contactPort);
        assertEquals(Status.OK, serviceBus.bindSessionPort(sessionPort, new SessionOpts(), listener));
    }

    public void tearDown() throws Exception {
        serviceBus.disconnect();
        serviceBus.release();
    }

    public class AboutListenerTestSessionPortListener extends SessionPortListener {
        public boolean acceptSessionJoiner(short sessionPort, String joiner, SessionOpts sessionOpts) {
            System.out.println("SessionPortListener.acceptSessionJoiner called");
            if (sessionPort == PORT_NUMBER) {
                return true;
            } else {
                return false;
            }
        }
        public void sessionJoined(short sessionPort, int id, String joiner) {
            System.out.println(String.format("SessionPortListener.sessionJoined(%d, %d, %s)", sessionPort, id, joiner));
            sessionId = id;
            sessionEstablished = true;
        }

        int sessionId;
        boolean sessionEstablished;
    }


    /*
     * An quick simple implementation of the AboutDataListener interface used
     * when making an announcement.
     */
    public class AboutListenerTestAboutData implements AboutDataListener {

        @Override
        public Map<String, Variant> getAboutData(String language) throws ErrorReplyBusException {
            Map<String, Variant> arg = new HashMap<String, Variant>();
            //nonlocalized values
            arg.put("AppId",  new Variant(new byte[] {1, 2, 3, 4, 5, 6, 7, 8, 9}));
            arg.put("DefaultLanguage",  new Variant(new String("en")));
            arg.put("DeviceId",  new Variant(new String("93c06771-c725-48c2-b1ff-6a2a59d445b8")));
            arg.put("ModelNumber", new Variant(new String("A1B2C3")));
            arg.put("SupportedLanguages", new Variant(new String[] {"en", "es"}));
            arg.put("DateOfManufacture", new Variant(new String("2014-09-23")));
            arg.put("SoftwareVersion", new Variant(new String("1.0")));
            arg.put("AJSoftwareVersion", new Variant(new String("0.0.1")));
            arg.put("HardwareVersion", new Variant(new String("0.1alpha")));
            arg.put("SupportUrl", new Variant(new String("http://www.example.com/support")));
            //localized values
            if((language == null) || (language.length() == 0) || language.equals("en")) {
                arg.put("DeviceName", new Variant(new String("A device name")));
                arg.put("AppName", new Variant(new String("An application name")));
                arg.put("Manufacturer", new Variant(new String("A mighty manufacturing company")));
                arg.put("Description", new Variant(new String("Sample showing the about feature in a service application")));
            } else if(language.equals("es")) { //Spanish
                arg.put("DeviceName", new Variant(new String("Un nombre de dispositivo")));
                arg.put("AppName", new Variant(new String("Un nombre de aplicación")));
                arg.put("Manufacturer", new Variant(new String("Una empresa de fabricación de poderosos")));
                arg.put("Description", new Variant(new String("Muestra que muestra la característica de sobre en una aplicación de servicio")));
            } else {
                throw new ErrorReplyBusException(Status.LANGUAGE_NOT_SUPPORTED);
            }
            return arg;
        }

        @Override
        public Map<String, Variant> getAnnouncedAboutData() throws ErrorReplyBusException {
            Map<String, Variant> announceArg = new HashMap<String, Variant>();
            announceArg.put("AppId",  new Variant(new byte[] {1, 2, 3, 4, 5, 6, 7, 8, 9}));
            announceArg.put("DefaultLanguage",  new Variant(new String("en")));
            announceArg.put("DeviceName", new Variant(new String("A device name")));
            announceArg.put("DeviceId",  new Variant(new String("93c06771-c725-48c2-b1ff-6a2a59d445b8")));
            announceArg.put("AppName", new Variant(new String("An application name")));
            announceArg.put("Manufacturer", new Variant(new String("A mighty manufacturing company")));
            announceArg.put("ModelNumber", new Variant(new String("A1B2C3")));
            return announceArg;
        }

    }

    class Intfa implements AboutListenerTestInterfaceA, BusObject {

        @Override
        public String echo(String str) throws BusException {
            return str;
        }
    }

    class Intfb implements AboutListenerTestInterfaceB, BusObject {

        @Override
        public String echo(String str) throws BusException {
            return str;
        }
    }
    class Intfc implements AboutListenerTestInterfaceC, BusObject {

        @Override
        public String echo(String str) throws BusException {
            return str;
        }
    }

    class AboutListenerTestAboutListener extends AboutListener {
        AboutListenerTestAboutListener() {
            port = 0;
            announcedFlag = false;
            aod = null;
        }
        public void announced(String busName, int version, short port, AboutObjectDescription[] objectDescriptions, Map<String, Variant> aboutData) {
            announcedFlag = true;
            this.port = port;
            aod = objectDescriptions;
            aod = new AboutObjectDescription[objectDescriptions.length];
            for (int i = 0; i < objectDescriptions.length; ++i) {
                aod[i] = objectDescriptions[i];
            }
            stopWait();
        }
        public short port;
        public boolean announcedFlag;
        public AboutObjectDescription[] aod;
    }

    public synchronized void testAnnounceTheAboutObj() {
        BusAttachment clientBus = new BusAttachment("AboutListenerTestClient", RemoteMessage.Receive);

        assertEquals(Status.OK, clientBus.connect());
        AboutListenerTestAboutListener al = new AboutListenerTestAboutListener();
        al.announcedFlag = false;
        clientBus.registerAboutListener(al);

        assertEquals(Status.OK, clientBus.whoImplements(new String[] {"org.alljoyn.About"}));

        AboutObj aboutObj = new AboutObj(serviceBus, true);
        AboutListenerTestAboutData aboutData = new AboutListenerTestAboutData();
        assertEquals(Status.OK, aboutObj.announce(PORT_NUMBER, aboutData));

        try {
            this.wait(10000);
        } catch (InterruptedException e) {
            fail("Unexpected failure when waiting for the announce singnal");
        }


        assertTrue(al.announcedFlag);
        boolean aboutPathFound = false;
        boolean aboutInterfaceFound = false;
        for (AboutObjectDescription aod : al.aod) {
            if (aod.path.equals("/About")) {
                aboutPathFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("org.alljoyn.About")) {
                        aboutInterfaceFound = true;
                    }
                }
            }
        }
        assertTrue(aboutPathFound);
        assertTrue(aboutInterfaceFound);

        assertEquals(Status.OK, clientBus.cancelWhoImplements(new String[] {"org.alljoyn.About"}));
        clientBus.disconnect();
        clientBus.release();
    }

    public synchronized void testCancelAnnouncement() {
        BusAttachment clientBus = new BusAttachment("AboutListenerTestClient", RemoteMessage.Receive);

        assertEquals(Status.OK, clientBus.connect());
        AboutListenerTestAboutListener al = new AboutListenerTestAboutListener();
        al.announcedFlag = false;
        clientBus.registerAboutListener(al);

        assertEquals(Status.OK, clientBus.whoImplements(new String[] {"org.alljoyn.About"}));

        AboutObj aboutObj = new AboutObj(serviceBus, true);
        AboutListenerTestAboutData aboutData = new AboutListenerTestAboutData();
        assertEquals(Status.OK, aboutObj.announce(PORT_NUMBER, aboutData));

        try {
            this.wait(10000);
        } catch (InterruptedException e) {
            fail("Unexpected failure when waiting for the announce singnal");
        }

        assertTrue(al.announcedFlag);
        boolean aboutPathFound = false;
        boolean aboutInterfaceFound = false;
        for (AboutObjectDescription aod : al.aod) {
            if (aod.path.equals("/About")) {
                aboutPathFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("org.alljoyn.About")) {
                        aboutInterfaceFound = true;
                    }
                }
            }
        }
        assertTrue(aboutPathFound);
        assertTrue(aboutInterfaceFound);

        assertEquals(Status.OK, aboutObj.cancelAnnouncement());
        assertEquals(Status.OK, clientBus.cancelWhoImplements(new String[] {"org.alljoyn.About"}));
        clientBus.disconnect();
        clientBus.release();
    }

    public synchronized void testRecieveAnnouncement() {
        Intfa intfa = new Intfa();
        assertEquals(Status.OK, serviceBus.registerBusObject(intfa, "/about/test"));

        BusAttachment clientBus = new BusAttachment("AboutListenerTestClient", RemoteMessage.Receive);
        assertEquals(Status.OK, clientBus.connect());

        AboutListenerTestAboutListener aListener = new AboutListenerTestAboutListener();
        aListener.announcedFlag = false;
        clientBus.registerAboutListener(aListener);
        assertEquals(Status.OK, clientBus.whoImplements(new String[] {"com.example.test.AboutListenerTest.a"}));

        AboutObj aboutObj = new AboutObj(serviceBus);
        AboutListenerTestAboutData aboutData = new AboutListenerTestAboutData();
        assertEquals(Status.OK, aboutObj.announce(PORT_NUMBER, aboutData));

        try {
            this.wait(10000);
        } catch (InterruptedException e) {
            fail("Unexpected failure when waiting for the announce singnal");
        }

        assertTrue(aListener.announcedFlag);
        boolean aboutPathFound = false;
        boolean aboutInterfaceFound = false;
        assertEquals(PORT_NUMBER, aListener.port);
        for (AboutObjectDescription aod : aListener.aod) {
            if (aod.path.equals("/about/test")) {
                aboutPathFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("com.example.test.AboutListenerTest.a")) {
                        aboutInterfaceFound = true;
                    }
                }
            }
        }
        assertTrue(aboutPathFound);
        assertTrue(aboutInterfaceFound);

        assertEquals(Status.OK, clientBus.cancelWhoImplements(new String[] {"com.example.test.AboutListenerTest.a"}));
        clientBus.disconnect();
        clientBus.release();
    }


    public synchronized void testNullWhoImplements() {
        Intfa intfa = new Intfa();
        assertEquals(Status.OK, serviceBus.registerBusObject(intfa, "/about/test"));

        BusAttachment clientBus = new BusAttachment("AboutListenerTestClient", RemoteMessage.Receive);
        assertEquals(Status.OK, clientBus.connect());

        AboutListenerTestAboutListener aListener = new AboutListenerTestAboutListener();
        aListener.announcedFlag = false;
        clientBus.registerAboutListener(aListener);
        assertEquals(Status.OK, clientBus.whoImplements(null));

        AboutObj aboutObj = new AboutObj(serviceBus);
        AboutListenerTestAboutData aboutData = new AboutListenerTestAboutData();
        assertEquals(Status.OK, aboutObj.announce(PORT_NUMBER, aboutData));

        try {
            this.wait(10000);
        } catch (InterruptedException e) {
            fail("Unexpected failure when waiting for the announce singnal");
        }

        assertTrue(aListener.announcedFlag);
        boolean aboutPathFound = false;
        boolean aboutInterfaceFound = false;
        assertEquals(PORT_NUMBER, aListener.port);
        for (AboutObjectDescription aod : aListener.aod) {
            if (aod.path.equals("/about/test")) {
                aboutPathFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("com.example.test.AboutListenerTest.a")) {
                        aboutInterfaceFound = true;
                    }
                }
            }
        }
        assertTrue(aboutPathFound);
        assertTrue(aboutInterfaceFound);

        assertEquals(Status.OK, clientBus.cancelWhoImplements(null));
        clientBus.disconnect();
        clientBus.release();
    }

    public synchronized void testWhoImplementsEmptyArray() {
        Intfa intfa = new Intfa();
        assertEquals(Status.OK, serviceBus.registerBusObject(intfa, "/about/test"));

        BusAttachment clientBus = new BusAttachment("AboutListenerTestClient", RemoteMessage.Receive);
        assertEquals(Status.OK, clientBus.connect());

        AboutListenerTestAboutListener aListener = new AboutListenerTestAboutListener();
        aListener.announcedFlag = false;
        clientBus.registerAboutListener(aListener);
        assertEquals(Status.OK, clientBus.whoImplements(new String[0]));

        AboutObj aboutObj = new AboutObj(serviceBus);
        AboutListenerTestAboutData aboutData = new AboutListenerTestAboutData();
        assertEquals(Status.OK, aboutObj.announce(PORT_NUMBER, aboutData));

        try {
            this.wait(10000);
        } catch (InterruptedException e) {
            fail("Unexpected failure when waiting for the announce singnal");
        }

        assertTrue(aListener.announcedFlag);
        boolean aboutPathFound = false;
        boolean aboutInterfaceFound = false;
        assertEquals(PORT_NUMBER, aListener.port);
        for (AboutObjectDescription aod : aListener.aod) {
            if (aod.path.equals("/about/test")) {
                aboutPathFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("com.example.test.AboutListenerTest.a")) {
                        aboutInterfaceFound = true;
                    }
                }
            }
        }
        assertTrue(aboutPathFound);
        assertTrue(aboutInterfaceFound);

        assertEquals(Status.OK, clientBus.cancelWhoImplements(new String[0]));
        clientBus.disconnect();
        clientBus.release();
    }

    public synchronized void testWhoImplementsBadArg() {
        Intfa intfa = new Intfa();
        assertEquals(Status.OK, serviceBus.registerBusObject(intfa, "/about/test"));

        BusAttachment clientBus = new BusAttachment("AboutListenerTestClient", RemoteMessage.Receive);
        assertEquals(Status.OK, clientBus.connect());

        AboutListenerTestAboutListener aListener = new AboutListenerTestAboutListener();
        aListener.announcedFlag = false;
        clientBus.registerAboutListener(aListener);
        assertEquals(Status.BAD_ARG_1, clientBus.whoImplements(new String[] {"com.example.test.AboutListenerTest.a", null, "org.alljoyn.About"}));

        assertEquals(Status.BAD_ARG_1, clientBus.cancelWhoImplements(new String[] {"com.example.test.AboutListenerTest.a", null, "org.alljoyn.About"}));
        clientBus.disconnect();
        clientBus.release();
    }

    public synchronized void testRecieveAnnouncementMultipleObjects() {
        Intfa intfa = new Intfa();
        assertEquals(Status.OK, serviceBus.registerBusObject(intfa, "/about/test/a"));
        Intfb intfb = new Intfb();
        assertEquals(Status.OK, serviceBus.registerBusObject(intfb, "/about/test/b"));
        Intfc intfc = new Intfc();
        assertEquals(Status.OK, serviceBus.registerBusObject(intfc, "/about/test/c"));

        BusAttachment clientBus = new BusAttachment("AboutListenerTestClient", RemoteMessage.Receive);
        assertEquals(Status.OK, clientBus.connect());

        AboutListenerTestAboutListener aListener = new AboutListenerTestAboutListener();
        aListener.announcedFlag = false;
        clientBus.registerAboutListener(aListener);
        assertEquals(Status.OK, clientBus.whoImplements(new String[] {"com.example.test.AboutListenerTest.a"}));

        AboutObj aboutObj = new AboutObj(serviceBus);
        AboutListenerTestAboutData aboutData = new AboutListenerTestAboutData();
        assertEquals(Status.OK, aboutObj.announce(PORT_NUMBER, aboutData));

        try {
            this.wait(10000);
        } catch (InterruptedException e) {
            fail("Unexpected failure when waiting for the announce singnal");
        }

        assertTrue(aListener.announcedFlag);
        boolean aboutPathAFound = false;
        boolean aboutInterfaceAFound = false;
        boolean aboutPathBFound = false;
        boolean aboutInterfaceBFound = false;
        boolean aboutPathCFound = false;
        boolean aboutInterfaceCFound = false;
        assertEquals(PORT_NUMBER, aListener.port);
        for (AboutObjectDescription aod : aListener.aod) {
            if (aod.path.equals("/about/test/a")) {
                aboutPathAFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("com.example.test.AboutListenerTest.a")) {
                        aboutInterfaceAFound = true;
                    }
                }
            }
            if (aod.path.equals("/about/test/b")) {
                aboutPathBFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("com.example.test.AboutListenerTest.b")) {
                        aboutInterfaceBFound = true;
                    }
                }
            }
            if (aod.path.equals("/about/test/c")) {
                aboutPathCFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("com.example.test.AboutListenerTest.c")) {
                        aboutInterfaceCFound = true;
                    }
                }
            }
        }
        assertTrue(aboutPathAFound);
        assertTrue(aboutInterfaceAFound);
        assertTrue(aboutPathBFound);
        assertTrue(aboutInterfaceBFound);
        assertTrue(aboutPathCFound);
        assertTrue(aboutInterfaceCFound);

        assertEquals(Status.OK, clientBus.cancelWhoImplements(new String[] {"com.example.test.AboutListenerTest.a"}));
        clientBus.disconnect();
        clientBus.release();
    }

    public synchronized void testReannounceAnnouncement() {
        Intfa intfa = new Intfa();
        assertEquals(Status.OK, serviceBus.registerBusObject(intfa, "/about/test"));

        BusAttachment clientBus = new BusAttachment("AboutListenerTestClient", RemoteMessage.Receive);
        assertEquals(Status.OK, clientBus.connect());

        AboutListenerTestAboutListener aListener = new AboutListenerTestAboutListener();
        aListener.announcedFlag = false;
        clientBus.registerAboutListener(aListener);
        assertEquals(Status.OK, clientBus.whoImplements(new String[] {"com.example.test.AboutListenerTest.a"}));

        AboutObj aboutObj = new AboutObj(serviceBus);
        AboutListenerTestAboutData aboutData = new AboutListenerTestAboutData();
        assertEquals(Status.OK, aboutObj.announce(PORT_NUMBER, aboutData));

        try {
            this.wait(10000);
        } catch (InterruptedException e) {
            fail("Unexpected failure when waiting for the announce singnal");
        }

        assertTrue(aListener.announcedFlag);
        assertTrue(aListener.announcedFlag);
        boolean aboutPathFound = false;
        boolean aboutInterfaceFound = false;
        assertEquals(PORT_NUMBER, aListener.port);
        for (AboutObjectDescription aod : aListener.aod) {
            if (aod.path.equals("/about/test")) {
                aboutPathFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("com.example.test.AboutListenerTest.a")) {
                        aboutInterfaceFound = true;
                    }
                }
            }
        }
        assertTrue(aboutPathFound);
        assertTrue(aboutInterfaceFound);

        aListener.announcedFlag = false;

        assertEquals(Status.OK, aboutObj.announce(PORT_NUMBER, aboutData));

        try {
            this.wait(10000);
        } catch (InterruptedException e) {
            fail("Unexpected failure when waiting for the announce singnal");
        }

        assertTrue(aListener.announcedFlag);

        assertTrue(aListener.announcedFlag);
        aboutPathFound = false;
        aboutInterfaceFound = false;
        assertEquals(PORT_NUMBER, aListener.port);
        for (AboutObjectDescription aod : aListener.aod) {
            if (aod.path.equals("/about/test")) {
                aboutPathFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("com.example.test.AboutListenerTest.a")) {
                        aboutInterfaceFound = true;
                    }
                }
            }
        }
        assertTrue(aboutPathFound);
        assertTrue(aboutInterfaceFound);

        assertEquals(Status.OK, clientBus.cancelWhoImplements(new String[] {"com.example.test.AboutListenerTest.a"}));
        clientBus.disconnect();
        clientBus.release();
    }

    public synchronized void testRemoveObjectDescriptionsFromAnnouncement() {
        Intfa intfa = new Intfa();
        assertEquals(Status.OK, serviceBus.registerBusObject(intfa, "/about/test/a"));
        Intfb intfb = new Intfb();
        assertEquals(Status.OK, serviceBus.registerBusObject(intfb, "/about/test/b"));
        Intfc intfc = new Intfc();
        assertEquals(Status.OK, serviceBus.registerBusObject(intfc, "/about/test/c"));

        BusAttachment clientBus = new BusAttachment("AboutListenerTestClient", RemoteMessage.Receive);
        assertEquals(Status.OK, clientBus.connect());

        AboutListenerTestAboutListener aListener = new AboutListenerTestAboutListener();
        aListener.announcedFlag = false;
        clientBus.registerAboutListener(aListener);
        assertEquals(Status.OK, clientBus.whoImplements(new String[] {"com.example.test.AboutListenerTest.c"}));

        AboutObj aboutObj = new AboutObj(serviceBus);
        AboutListenerTestAboutData aboutData = new AboutListenerTestAboutData();
        assertEquals(Status.OK, aboutObj.announce(PORT_NUMBER, aboutData));

        try {
            this.wait(10000);
        } catch (InterruptedException e) {
            fail("Unexpected failure when waiting for the announce singnal");
        }

        assertTrue(aListener.announcedFlag);
        boolean aboutPathAFound = false;
        boolean aboutInterfaceAFound = false;
        boolean aboutPathBFound = false;
        boolean aboutInterfaceBFound = false;
        boolean aboutPathCFound = false;
        boolean aboutInterfaceCFound = false;
        assertEquals(PORT_NUMBER, aListener.port);
        for (AboutObjectDescription aod : aListener.aod) {
            if (aod.path.equals("/about/test/a")) {
                aboutPathAFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("com.example.test.AboutListenerTest.a")) {
                        aboutInterfaceAFound = true;
                    }
                }
            }
            if (aod.path.equals("/about/test/b")) {
                aboutPathBFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("com.example.test.AboutListenerTest.b")) {
                        aboutInterfaceBFound = true;
                    }
                }
            }
            if (aod.path.equals("/about/test/c")) {
                aboutPathCFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("com.example.test.AboutListenerTest.c")) {
                        aboutInterfaceCFound = true;
                    }
                }
            }
        }
        assertTrue(aboutPathAFound);
        assertTrue(aboutInterfaceAFound);
        assertTrue(aboutPathBFound);
        assertTrue(aboutInterfaceBFound);
        assertTrue(aboutPathCFound);
        assertTrue(aboutInterfaceCFound);


        serviceBus.unregisterBusObject(intfa);
        serviceBus.unregisterBusObject(intfb);

        //reset flags to false values and announce again
        aListener.announcedFlag = false;

        assertEquals(Status.OK, aboutObj.announce(PORT_NUMBER, aboutData));

        try {
            this.wait(10000);
        } catch (InterruptedException e) {
            fail("Unexpected failure when waiting for the announce singnal");
        }

        assertTrue(aListener.announcedFlag);

        aboutPathAFound = false;
        aboutInterfaceAFound = false;
        aboutPathBFound = false;
        aboutInterfaceBFound = false;
        aboutPathCFound = false;
        aboutInterfaceCFound = false;

        assertEquals(PORT_NUMBER, aListener.port);
        for (AboutObjectDescription aod : aListener.aod) {
            if (aod.path.equals("/about/test/a")) {
                aboutPathAFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("com.example.test.AboutListenerTest.a")) {
                        aboutInterfaceAFound = true;
                    }
                }
            }
            if (aod.path.equals("/about/test/b")) {
                aboutPathBFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("com.example.test.AboutListenerTest.b")) {
                        aboutInterfaceBFound = true;
                    }
                }
            }
            if (aod.path.equals("/about/test/c")) {
                aboutPathCFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("com.example.test.AboutListenerTest.c")) {
                        aboutInterfaceCFound = true;
                    }
                }
            }
        }
        assertFalse(aboutPathAFound);
        assertFalse(aboutInterfaceAFound);
        assertFalse(aboutPathBFound);
        assertFalse(aboutInterfaceBFound);
        assertTrue(aboutPathCFound);
        assertTrue(aboutInterfaceCFound);

        assertEquals(Status.OK, clientBus.cancelWhoImplements(new String[] {"com.example.test.AboutListenerTest.c"}));
        clientBus.disconnect();
        clientBus.release();
    }

    // ASACORE-1033 hangs when calling cancelWhoImplments
    public synchronized void DISABLED_testMultipleAboutListeners() {
        Intfa intfa = new Intfa();
        assertEquals(Status.OK, serviceBus.registerBusObject(intfa, "/about/test"));

        BusAttachment clientBus = new BusAttachment("AboutListenerTestClient", RemoteMessage.Receive);
        assertEquals(Status.OK, clientBus.connect());

        AboutListenerTestAboutListener aListener1 = new AboutListenerTestAboutListener();
        aListener1.announcedFlag = false;
        clientBus.registerAboutListener(aListener1);
        AboutListenerTestAboutListener aListener2 = new AboutListenerTestAboutListener();
        aListener2.announcedFlag = false;
        clientBus.registerAboutListener(aListener1);
        AboutListenerTestAboutListener aListener3 = new AboutListenerTestAboutListener();
        aListener3.announcedFlag = false;
        clientBus.registerAboutListener(aListener1);
        clientBus.registerAboutListener(aListener2);
        clientBus.registerAboutListener(aListener3);
        assertEquals(Status.OK, clientBus.whoImplements(new String[] {"com.example.test.AboutListenerTest.a"}));

        AboutObj aboutObj = new AboutObj(serviceBus);
        AboutListenerTestAboutData aboutData = new AboutListenerTestAboutData();
        assertEquals(Status.OK, aboutObj.announce(PORT_NUMBER, aboutData));

        // There are three callbacks so notify will be called three times we
        // add a wait for each callback. Since the code contains the Syncronized
        // key word for the wait/notify calls we should not have any issues with
        // notify being called multiple times for one call to wait.  It is a
        // possibility but the worst outcome will be that we wait for the full
        // timeout.  Making the test run long.
        try {
            this.wait(10000);
        } catch (InterruptedException e) {
            fail("Unexpected failure when waiting for the announce singnal");
        }
        try {
            this.wait(5000);
        } catch (InterruptedException e) {
            fail("Unexpected failure when waiting for the announce singnal");
        }
        try {
            this.wait(5000);
        } catch (InterruptedException e) {
            fail("Unexpected failure when waiting for the announce singnal");
        }

        assertTrue(aListener1.announcedFlag);
        boolean aboutPathFound = false;
        boolean aboutInterfaceFound = false;
        assertEquals(PORT_NUMBER, aListener1.port);
        for (AboutObjectDescription aod : aListener1.aod) {
            if (aod.path.equals("/about/test")) {
                aboutPathFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("com.example.test.AboutListenerTest.a")) {
                        aboutInterfaceFound = true;
                    }
                }
            }
        }
        assertTrue(aboutPathFound);
        assertTrue(aboutInterfaceFound);

        assertTrue(aListener2.announcedFlag);
        aboutPathFound = false;
        aboutInterfaceFound = false;
        assertEquals(PORT_NUMBER, aListener2.port);
        for (AboutObjectDescription aod : aListener2.aod) {
            if (aod.path.equals("/about/test")) {
                aboutPathFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("com.example.test.AboutListenerTest.a")) {
                        aboutInterfaceFound = true;
                    }
                }
            }
        }
        assertTrue(aboutPathFound);
        assertTrue(aboutInterfaceFound);

        assertTrue(aListener3.announcedFlag);
        aboutPathFound = false;
        aboutInterfaceFound = false;
        assertEquals(PORT_NUMBER, aListener3.port);
        for (AboutObjectDescription aod : aListener3.aod) {
            if (aod.path.equals("/about/test")) {
                aboutPathFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("com.example.test.AboutListenerTest.a")) {
                        aboutInterfaceFound = true;
                    }
                }
            }
        }
        assertTrue(aboutPathFound);
        assertTrue(aboutInterfaceFound);

        assertEquals(Status.OK, clientBus.cancelWhoImplements(new String[] {"com.example.test.AboutListenerTest.a"}));
        clientBus.disconnect();
        clientBus.release();
    }

    // ASACORE-1037 unregister fails in some instances
    public synchronized void DISABLED_testMultipleAboutListenersUnregisterSome() {
        Intfa intfa = new Intfa();
        assertEquals(Status.OK, serviceBus.registerBusObject(intfa, "/about/test"));

        BusAttachment clientBus = new BusAttachment("AboutListenerTestClient", RemoteMessage.Receive);
        assertEquals(Status.OK, clientBus.connect());

        AboutListenerTestAboutListener aListener1 = new AboutListenerTestAboutListener();
        aListener1.announcedFlag = false;
        clientBus.registerAboutListener(aListener1);
        AboutListenerTestAboutListener aListener2 = new AboutListenerTestAboutListener();
        aListener2.announcedFlag = false;
        clientBus.registerAboutListener(aListener1);
        AboutListenerTestAboutListener aListener3 = new AboutListenerTestAboutListener();
        aListener3.announcedFlag = false;
        clientBus.registerAboutListener(aListener1);
        clientBus.registerAboutListener(aListener2);
        clientBus.registerAboutListener(aListener3);
        assertEquals(Status.OK, clientBus.whoImplements(new String[] {"com.example.test.AboutListenerTest.a"}));

        AboutObj aboutObj = new AboutObj(serviceBus);
        AboutListenerTestAboutData aboutData = new AboutListenerTestAboutData();
        assertEquals(Status.OK, aboutObj.announce(PORT_NUMBER, aboutData));

        // There are three callbacks so notify will be called three times we
        // add a wait for each callback. Since the code contains the Syncronized
        // key word for the wait/notify calls we should not have any issues with
        // notify being called multiple times for one call to wait.  It is a
        // possibility but the worst outcome will be that we wait for the full
        // timeout.  Making the test run long.
        try {
            this.wait(10000);
        } catch (InterruptedException e) {
            fail("Unexpected failure when waiting for the announce singnal");
        }
        try {
            this.wait(5000);
        } catch (InterruptedException e) {
            fail("Unexpected failure when waiting for the announce singnal");
        }
        try {
            this.wait(5000);
        } catch (InterruptedException e) {
            fail("Unexpected failure when waiting for the announce singnal");
        }

        assertTrue(aListener1.announcedFlag);
        boolean aboutPathFound = false;
        boolean aboutInterfaceFound = false;
        assertEquals(PORT_NUMBER, aListener1.port);
        for (AboutObjectDescription aod : aListener1.aod) {
            if (aod.path.equals("/about/test")) {
                aboutPathFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("com.example.test.AboutListenerTest.a")) {
                        aboutInterfaceFound = true;
                    }
                }
            }
        }
        assertTrue(aboutPathFound);
        assertTrue(aboutInterfaceFound);

        assertTrue(aListener2.announcedFlag);
        aboutPathFound = false;
        aboutInterfaceFound = false;
        assertEquals(PORT_NUMBER, aListener2.port);
        for (AboutObjectDescription aod : aListener2.aod) {
            if (aod.path.equals("/about/test")) {
                aboutPathFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("com.example.test.AboutListenerTest.a")) {
                        aboutInterfaceFound = true;
                    }
                }
            }
        }
        assertTrue(aboutPathFound);
        assertTrue(aboutInterfaceFound);

        assertTrue(aListener3.announcedFlag);
        aboutPathFound = false;
        aboutInterfaceFound = false;
        assertEquals(PORT_NUMBER, aListener3.port);
        for (AboutObjectDescription aod : aListener3.aod) {
            if (aod.path.equals("/about/test")) {
                aboutPathFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("com.example.test.AboutListenerTest.a")) {
                        aboutInterfaceFound = true;
                    }
                }
            }
        }
        assertTrue(aboutPathFound);
        assertTrue(aboutInterfaceFound);


        aListener1.announcedFlag = false;
        aListener2.announcedFlag = false;
        aListener3.announcedFlag = false;

        clientBus.unregisterAboutListener(aListener1);
        clientBus.unregisterAboutListener(aListener3);

        assertEquals(Status.OK, aboutObj.announce(PORT_NUMBER, aboutData));
        try {
            this.wait(10000);
        } catch (InterruptedException e) {
            fail("Unexpected failure when waiting for the announce singnal");
        }

        assertFalse(aListener1.announcedFlag);
        assertTrue(aListener2.announcedFlag);
        assertFalse(aListener3.announcedFlag);

        assertEquals(Status.OK, clientBus.cancelWhoImplements(new String[] {"com.example.test.AboutListenerTest.a"}));
        clientBus.disconnect();
        clientBus.release();
    }

    class Intfabc implements AboutListenerTestInterfaceA,
                             AboutListenerTestInterfaceB,
                             AboutListenerTestInterfaceC,
                             BusObject {

        @Override
        public String echo(String str) throws BusException {
            return str;
        }
    }

    public synchronized void testWhoImplementsMultipleInterfaces() {
        Intfabc intfabc = new Intfabc();
        assertEquals(Status.OK, serviceBus.registerBusObject(intfabc, "/about/test"));

        BusAttachment clientBus = new BusAttachment("AboutListenerTestClient", RemoteMessage.Receive);
        assertEquals(Status.OK, clientBus.connect());

        AboutListenerTestAboutListener aListener = new AboutListenerTestAboutListener();
        aListener.announcedFlag = false;
        clientBus.registerAboutListener(aListener);
        assertEquals(Status.OK, clientBus.whoImplements(new String[] {"com.example.test.AboutListenerTest.a", "com.example.test.AboutListenerTest.b", "com.example.test.AboutListenerTest.c"}));

        AboutObj aboutObj = new AboutObj(serviceBus);
        AboutListenerTestAboutData aboutData = new AboutListenerTestAboutData();
        assertEquals(Status.OK, aboutObj.announce(PORT_NUMBER, aboutData));

        try {
            this.wait(10000);
        } catch (InterruptedException e) {
            fail("Unexpected failure when waiting for the announce singnal");
        }

        assertTrue(aListener.announcedFlag);

        boolean aboutPathFound = false;
        boolean aboutInterfaceAFound = false;
        boolean aboutInterfaceBFound = false;
        boolean aboutInterfaceCFound = false;
        assertEquals(PORT_NUMBER, aListener.port);
        for (AboutObjectDescription aod : aListener.aod) {
            if (aod.path.equals("/about/test")) {
                aboutPathFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("com.example.test.AboutListenerTest.a")) {
                        aboutInterfaceAFound = true;
                    }
                    if (s.equals("com.example.test.AboutListenerTest.b")) {
                        aboutInterfaceBFound = true;
                    }
                    if (s.equals("com.example.test.AboutListenerTest.c")) {
                        aboutInterfaceCFound = true;
                    }
                }
            }
        }
        assertTrue(aboutPathFound);
        assertTrue(aboutInterfaceAFound);
        assertTrue(aboutInterfaceBFound);
        assertTrue(aboutInterfaceCFound);


        assertEquals(Status.OK, clientBus.cancelWhoImplements(new String[] {"com.example.test.AboutListenerTest.a", "com.example.test.AboutListenerTest.b", "com.example.test.AboutListenerTest.c"}));
        clientBus.disconnect();
        clientBus.release();
    }

    class Intfabcdef implements AboutListenerTestInterfaceA,
                                AboutListenerTestInterfaceB,
                                AboutListenerTestInterfaceC,
                                AboutListenerTestInterfaceD,
                                AboutListenerTestInterfaceE,
                                AboutListenerTestInterfaceF,
                                BusObject {

        @Override
        public String echo(String str) throws BusException {
            return str;
        }
    }

    public synchronized void testWhoImplementsMultipleInterfacesSubSet() {
        Intfabcdef intfabcdef = new Intfabcdef();
        assertEquals(Status.OK, serviceBus.registerBusObject(intfabcdef, "/about/test"));

        BusAttachment clientBus = new BusAttachment("AboutListenerTestClient", RemoteMessage.Receive);
        assertEquals(Status.OK, clientBus.connect());

        AboutListenerTestAboutListener aListener = new AboutListenerTestAboutListener();
        aListener.announcedFlag = false;
        clientBus.registerAboutListener(aListener);
        assertEquals(Status.OK, clientBus.whoImplements(new String[] {"com.example.test.AboutListenerTest.b", "com.example.test.AboutListenerTest.e"}));

        AboutObj aboutObj = new AboutObj(serviceBus);
        AboutListenerTestAboutData aboutData = new AboutListenerTestAboutData();
        assertEquals(Status.OK, aboutObj.announce(PORT_NUMBER, aboutData));

        try {
            this.wait(10000);
        } catch (InterruptedException e) {
            fail("Unexpected failure when waiting for the announce singnal");
        }

        assertTrue(aListener.announcedFlag);

        boolean aboutPathFound = false;
        boolean aboutInterfaceBFound = false;
        boolean aboutInterfaceEFound = false;
        assertEquals(PORT_NUMBER, aListener.port);
        for (AboutObjectDescription aod : aListener.aod) {
            if (aod.path.equals("/about/test")) {
                aboutPathFound = true;
                for (String s : aod.interfaces) {
                    if (s.equals("com.example.test.AboutListenerTest.b")) {
                        aboutInterfaceBFound = true;
                    }
                    if (s.equals("com.example.test.AboutListenerTest.e")) {
                        aboutInterfaceEFound = true;
                    }
                }
            }
        }
        assertTrue(aboutPathFound);
        assertTrue(aboutInterfaceBFound);
        assertTrue(aboutInterfaceEFound);

        assertEquals(Status.OK, clientBus.cancelWhoImplements(new String[] {"com.example.test.AboutListenerTest.b", "com.example.test.AboutListenerTest.e"}));
        clientBus.disconnect();
        clientBus.release();
    }

    public synchronized void testWhoImplementsWildCardMatch() {
        Intfabc intfabc = new Intfabc();
        assertEquals(Status.OK, serviceBus.registerBusObject(intfabc, "/about/test"));

        BusAttachment clientBus = new BusAttachment("AboutListenerTestClient", RemoteMessage.Receive);
        assertEquals(Status.OK, clientBus.connect());

        AboutListenerTestAboutListener aListener = new AboutListenerTestAboutListener();
        aListener.announcedFlag = false;
        clientBus.registerAboutListener(aListener);
        assertEquals(Status.OK, clientBus.whoImplements(new String[] {"com.example.test.AboutListenerTest.*"}));

        AboutObj aboutObj = new AboutObj(serviceBus);
        AboutListenerTestAboutData aboutData = new AboutListenerTestAboutData();
        assertEquals(Status.OK, aboutObj.announce(PORT_NUMBER, aboutData));

        try {
            this.wait(10000);
        } catch (InterruptedException e) {
            fail("Unexpected failure when waiting for the announce singnal");
        }

        assertTrue(aListener.announcedFlag);

        boolean aboutPathFound = false;
        boolean aboutInterfaceFound = false;
        assertEquals(PORT_NUMBER, aListener.port);
        for (AboutObjectDescription aod : aListener.aod) {
            if (aod.path.equals("/about/test")) {
                aboutPathFound = true;
                for (String s : aod.interfaces) {
                    if (s.contains("com.example.test.AboutListenerTest")) {
                        aboutInterfaceFound = true;
                    }
                }
            }
        }
        assertTrue(aboutPathFound);
        assertTrue(aboutInterfaceFound);

        assertEquals(Status.OK, clientBus.cancelWhoImplements(new String[] {"com.example.test.AboutListenerTest.*"}));
        clientBus.disconnect();
        clientBus.release();
    }
    /*
     * Negative test ASACORE-1020
     *  ASACORE-1020 cancelWhoImplements mismatch whoImplements
     */
    public synchronized void testCancelImplementsMisMatch() {
        BusAttachment clientBus = new BusAttachment("AboutListenerTestClient", RemoteMessage.Receive);
        assertEquals(Status.OK, clientBus.connect());

        assertEquals(Status.OK, clientBus.whoImplements(new String[] {"com.example.test.AboutListenerTest.*"}));

        assertEquals(Status.BUS_MATCH_RULE_NOT_FOUND, clientBus.cancelWhoImplements(new String[] {"com.example.test.AboutListenerTest.a"}));
        clientBus.disconnect();
        clientBus.release();
    }
}
