package org.mavlink.qgroundcontrol.voicerelay;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.os.IBinder;
import android.os.SystemClock;
import android.util.Log;

import java.io.IOException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.SocketAddress;
import java.net.StandardProtocolFamily;
import java.net.UnknownHostException;
import java.nio.ByteBuffer;
import java.nio.channels.DatagramChannel;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;

/**
 * Carries the ground station's live voice to the loudspeaker payload over Tailscale.
 *
 * The station is excluded from Tailscale so that its KCMVP bridge can tie sockets to the USB
 * Ethernet adapter, and an app outside the VPN has no route to a Tailscale address. This app
 * stays inside it. It takes the station's datagrams on loopback, on the two ports the payload
 * daemon listens on, and sends them on from a single socket, so the daemon sees state requests
 * and audio from one peer, as it would from the station itself. Replies from the payload go back
 * to whoever last sent on the same port.
 *
 * It never opens the microphone: the station records, this only forwards.
 */
public final class RelayService extends Service {
    private static final String TAG = "VoiceRelay";

    static final int COMMAND_PORT = 37270;
    static final int AUDIO_PORT = COMMAND_PORT + 1;
    static final String DEFAULT_TARGET = "100.101.106.21";

    // Fixed and below the ephemeral range, so it can never take a port the station binds after it.
    private static final int UPSTREAM_PORT = 17270;
    private static final String PREFS = "relay";
    private static final String KEY_TARGET = "target";
    private static final String CHANNEL_ID = "relay";
    private static final int NOTIFICATION_ID = 1;
    private static final long RETRY_MSECS = 2000;
    private static final int MAX_DATAGRAM = 2048;

    // Written by the relay thread and the network callback, shown by MainActivity.
    static volatile boolean s_running;
    static volatile boolean s_tailscaleUp;
    static volatile long s_forwarded;
    static volatile long s_held;
    static volatile long s_replies;
    static volatile long s_lastReplyAt;
    static volatile String s_lastError = "";

    private volatile boolean m_stopping;
    private volatile boolean m_retarget = true;
    private volatile Selector m_selector;
    private Thread m_worker;
    private ConnectivityManager.NetworkCallback m_networkCallback;

    static String target(final Context context) {
        return context.getSharedPreferences(PREFS, MODE_PRIVATE).getString(KEY_TARGET, DEFAULT_TARGET);
    }

    static void setTarget(final Context context, final String target) {
        context.getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString(KEY_TARGET, target).apply();
    }

    static void start(final Context context) {
        context.startForegroundService(new Intent(context, RelayService.class));
    }

    /**
     * The payload's address if the text is a plain IPv4 address, else null. A name would need DNS
     * that is not up at boot, the upstream socket is IPv4 only, and a local address would send
     * the station's datagrams straight back into this relay.
     */
    static InetAddress parseTarget(final String text) {
        final String[] parts = text.trim().split("\\.", -1);
        if (parts.length != 4) {
            return null;
        }
        final byte[] bytes = new byte[4];
        for (int i = 0; i < 4; i++) {
            final String part = parts[i];
            if (part.isEmpty() || (part.length() > 3)) {
                return null;
            }
            for (int c = 0; c < part.length(); c++) {
                if ((part.charAt(c) < '0') || (part.charAt(c) > '9')) {
                    return null;
                }
            }
            final int value = Integer.parseInt(part);
            if (value > 255) {
                return null;
            }
            bytes[i] = (byte) value;
        }
        try {
            final InetAddress address = InetAddress.getByAddress(bytes);
            if (address.isLoopbackAddress() || address.isAnyLocalAddress() || address.isMulticastAddress()) {
                return null;
            }
            return address;
        } catch (UnknownHostException e) {
            return null;
        }
    }

    @Override
    public void onCreate() {
        super.onCreate();
        final NotificationManager manager = getSystemService(NotificationManager.class);
        manager.createNotificationChannel(
            new NotificationChannel(CHANNEL_ID, "음성 중계", NotificationManager.IMPORTANCE_LOW));

        // With Tailscale down the payload's address leads to some other host on the carrier's
        // network, so nothing is sent unless this app's default network is the VPN.
        m_networkCallback = new ConnectivityManager.NetworkCallback() {
            @Override
            public void onCapabilitiesChanged(final Network network, final NetworkCapabilities capabilities) {
                s_tailscaleUp = capabilities.hasTransport(NetworkCapabilities.TRANSPORT_VPN);
            }

            @Override
            public void onLost(final Network network) {
                s_tailscaleUp = false;
            }
        };
        getSystemService(ConnectivityManager.class).registerDefaultNetworkCallback(m_networkCallback);
    }

    @Override
    public int onStartCommand(final Intent intent, final int flags, final int startId) {
        try {
            startForeground(NOTIFICATION_ID, notification());
        } catch (RuntimeException e) {
            // Android 12+ can refuse a restart the system makes on its own; the relay then waits
            // for the next boot or for the app to be opened.
            s_lastError = e.toString();
            Log.w(TAG, "could not come to the foreground: " + e);
            stopSelf();
            return START_NOT_STICKY;
        }
        // Every start re-reads the target: this is how MainActivity applies a new address.
        m_retarget = true;
        final Selector selector = m_selector;
        if (selector != null) {
            selector.wakeup();
        }
        if (m_worker == null) {
            m_worker = new Thread(this::relay, "VoiceRelay");
            m_worker.start();
        }
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        m_stopping = true;
        final Selector selector = m_selector;
        if (selector != null) {
            selector.wakeup();
        }
        if (m_networkCallback != null) {
            getSystemService(ConnectivityManager.class).unregisterNetworkCallback(m_networkCallback);
            m_networkCallback = null;
        }
        super.onDestroy();
    }

    @Override
    public IBinder onBind(final Intent intent) {
        return null;
    }

    private Notification notification() {
        final PendingIntent open = PendingIntent.getActivity(
            this, 0, new Intent(this, MainActivity.class), PendingIntent.FLAG_IMMUTABLE);
        return new Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.stat_sys_upload_done)
            .setContentTitle("음성 중계 대기 중")
            .setContentText("기체 스피커 " + target(this) + " (Tailscale)")
            .setContentIntent(open)
            .setOngoing(true)
            .build();
    }

    private void relay() {
        while (!m_stopping) {
            // A rebuilt loop starts with no target, so it has to read it again.
            m_retarget = true;
            try {
                relayUntilError();
            } catch (IOException | RuntimeException e) {
                // Runtime errors too: an uncaught one would take the whole relay down with this thread.
                s_lastError = e.toString();
                Log.w(TAG, "relay stopped, rebuilding: " + e);
            }
            s_running = false;
            if (!m_stopping) {
                SystemClock.sleep(RETRY_MSECS);
            }
        }
    }

    private void relayUntilError() throws IOException {
        try (Selector selector = Selector.open();
             DatagramChannel command = openLoopback(COMMAND_PORT);
             DatagramChannel audio = openLoopback(AUDIO_PORT);
             DatagramChannel upstream = DatagramChannel.open(StandardProtocolFamily.INET)) {
            // Spelled out: Android's wildcard is IPv6, which an IPv4 channel refuses.
            upstream.bind(new InetSocketAddress(InetAddress.getByAddress(new byte[] {0, 0, 0, 0}), UPSTREAM_PORT));
            upstream.configureBlocking(false);
            command.register(selector, SelectionKey.OP_READ);
            audio.register(selector, SelectionKey.OP_READ);
            upstream.register(selector, SelectionKey.OP_READ);
            m_selector = selector;

            final ByteBuffer buffer = ByteBuffer.allocate(MAX_DATAGRAM);
            InetAddress payload = null;
            SocketAddress commandClient = null;
            SocketAddress audioClient = null;
            while (!m_stopping) {
                if (m_retarget) {
                    m_retarget = false;
                    payload = parseTarget(target(this));
                    s_running = true;
                    s_lastError = (payload == null) ? "기체 스피커 주소가 올바른 IPv4 주소가 아닙니다" : "";
                }
                selector.select(1000);
                for (final SelectionKey key : selector.selectedKeys()) {
                    final DatagramChannel channel = (DatagramChannel) key.channel();
                    buffer.clear();
                    final SocketAddress from = channel.receive(buffer);
                    if (from == null) {
                        continue;
                    }
                    buffer.flip();
                    if (channel == upstream) {
                        final InetSocketAddress source = (InetSocketAddress) from;
                        // Only the payload's answers go back to the station.
                        if ((payload == null) || !source.getAddress().equals(payload)) {
                            continue;
                        }
                        if ((source.getPort() == COMMAND_PORT) && (commandClient != null)) {
                            reply(command, buffer, commandClient);
                            s_replies++;
                            s_lastReplyAt = SystemClock.elapsedRealtime();
                        } else if ((source.getPort() == AUDIO_PORT) && (audioClient != null)) {
                            reply(audio, buffer, audioClient);
                        }
                    } else if (channel == command) {
                        commandClient = from;
                        forward(upstream, buffer, payload, COMMAND_PORT);
                    } else {
                        audioClient = from;
                        forward(upstream, buffer, payload, AUDIO_PORT);
                    }
                }
                selector.selectedKeys().clear();
            }
        } finally {
            m_selector = null;
        }
    }

    // A send refused while the network changes costs that datagram, not the sockets.
    private static void forward(final DatagramChannel upstream, final ByteBuffer datagram,
                                final InetAddress payload, final int port) {
        if ((payload == null) || !s_tailscaleUp) {
            s_held++;
            return;
        }
        try {
            upstream.send(datagram, new InetSocketAddress(payload, port));
            s_forwarded++;
        } catch (IOException e) {
            s_lastError = e.toString();
        }
    }

    private static void reply(final DatagramChannel channel, final ByteBuffer datagram, final SocketAddress client) {
        try {
            channel.send(datagram, client);
        } catch (IOException e) {
            s_lastError = e.toString();
        }
    }

    private static DatagramChannel openLoopback(final int port) throws IOException {
        final DatagramChannel channel = DatagramChannel.open(StandardProtocolFamily.INET);
        try {
            // Loopback only: nothing off the handset can reach the payload through this app.
            channel.bind(new InetSocketAddress(InetAddress.getByAddress(new byte[] {127, 0, 0, 1}), port));
            channel.configureBlocking(false);
            return channel;
        } catch (IOException e) {
            channel.close();
            throw e;
        }
    }
}
