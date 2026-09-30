package org.mavlink.qgroundcontrol;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Binder;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.os.RemoteException;
import android.os.SystemClock;

import java.util.Arrays;

/**
 * Reads the UniRC 7 Pro handset's own channel outputs for the police handset buttons (PoliceRcButtons).
 *
 * The same values reach the flight controller over S.Bus and come back in RC_CHANNELS, but only while the drone is
 * powered and linked, and slowly. UniGCS's channel page reads them from the SIYI remote-control service instead,
 * reached here the way PoliceRcLinkMonitor reaches it. Read off UniGCS 3.1.4 and 3.2.0, which agree on these codes:
 *   control tx 1  = register an IRemoteCallback binder
 *   control tx 40 = start the channel stream; it restarts the service's collector, which sends the current values
 *   callback 8    = the 16 channel outputs in microseconds, a List of ChannelValue: a count, then per item a
 *                   non-null flag, the RCChannel enum name ("CHANNEL_1".."CHANNEL_16") and the value
 * The service sends a list only when a value changes, so a quiet stream is not a dead one. tx 40 is therefore sent
 * again every few seconds, and twice a second while the talk key is held: each is answered at once, and a poke with
 * no answer means the stream is gone (UniGCS's tx 41 stops it for everyone; a restarted service forgets it).
 * An empty list is the service's placeholder until the handset reports, so it carries no reading.
 * Nothing calls tx 41 (stop): UniGCS's channel page shares the stream.
 */
public final class PoliceRcChannelMonitor {
    private static final String TAG = "PoliceRcChannels";
    private static final String PACKAGE = "biz.siyi.remotecontrol";
    private static final String SERVICE = "biz.siyi.pilot.rcuservice.RemoteControlService";
    private static final String CONTROL = "biz.siyi.pilot.rcuservice.IRemoteControl";
    private static final String CALLBACK = "biz.siyi.pilot.rcuservice.callback.IRemoteCallback";
    private static final int TX_REGISTER = 1;
    private static final int TX_CHANNELS_START = 40;
    private static final int CB_CHANNELS = 8;
    private static final int CHANNEL_COUNT = 16;
    private static final String CHANNEL_PREFIX = "CHANNEL_";
    private static final long POKE_IDLE_MS = 3000;
    private static final long POKE_HELD_MS = 500;
    private static final long ANSWER_MS = 400;
    private static final long BACKOFF_MAX_MS = 30000;

    private static Context s_context;
    private static ServiceConnection s_connection;
    // Main thread only, except the two volatiles the callback writes.
    private static IBinder s_binder;
    private static volatile long s_lastAnswer;
    private static volatile boolean s_loggedFirst;
    private static long s_pokedAt;
    private static boolean s_live;
    private static boolean s_talkHeld;
    private static long s_backoffMs = 1000;
    private static final Handler s_handler = new Handler(Looper.getMainLooper());

    /** Index 0 is CH1; -1 means the service did not report that channel. Called on a binder thread. */
    static native void nativeChannels(int[] values);
    /** The stream stopped answering or the service went away. Called on the main thread. */
    static native void nativeLost();

    private PoliceRcChannelMonitor() {}

    /** Bind the SIYI remote-control service. Idempotent; call after the natives are registered. */
    public static synchronized void start(final Context context) {
        if (s_context != null) {
            return;
        }
        s_context = context.getApplicationContext();
        s_handler.post(REBIND);
    }

    /** While the talk key is held a lost stream must be noticed quickly, since only a reading can end the talk. */
    public static void setTalkHeld(final boolean held) {
        s_handler.post(new Runnable() {
            @Override
            public void run() {
                if (held != s_talkHeld) {
                    s_talkHeld = held;
                    if (held) {
                        s_handler.removeCallbacks(POKE);
                        s_handler.post(POKE);
                    }
                }
            }
        });
    }

    private static final Runnable POKE = new Runnable() {
        @Override
        public void run() {
            final IBinder binder = s_binder;
            if (binder != null) {
                s_pokedAt = SystemClock.elapsedRealtime();
                call(binder, TX_CHANNELS_START, false);
                s_handler.removeCallbacks(CHECK);
                s_handler.postDelayed(CHECK, ANSWER_MS);
            }
            s_handler.postDelayed(this, s_talkHeld ? POKE_HELD_MS : POKE_IDLE_MS);
        }
    };

    private static final Runnable CHECK = new Runnable() {
        @Override
        public void run() {
            final boolean answered = s_lastAnswer >= s_pokedAt;
            if (answered != s_live) {
                s_live = answered;
                QGCLogger.i(TAG, answered ? "channel stream answering" : "channel stream not answering");
            }
            if (!answered) {
                lost();
            }
        }
    };

    private static final Runnable REBIND = new Runnable() {
        @Override
        public void run() {
            bind();
        }
    };

    private static void bind() {
        // Never startService: SIYI starts this service at boot.
        final Intent intent = new Intent();
        intent.setClassName(PACKAGE, SERVICE);

        final ServiceConnection connection = new ServiceConnection() {
            @Override
            public void onServiceConnected(ComponentName name, IBinder service) {
                String descriptor = null;
                try {
                    descriptor = service.getInterfaceDescriptor();
                } catch (RemoteException e) {
                    // Checked below.
                }
                if (!CONTROL.equals(descriptor)) {
                    QGCLogger.w(TAG, "remote-control service speaks " + descriptor + ", not " + CONTROL);
                    return;
                }
                QGCLogger.i(TAG, "remote-control service connected");
                s_binder = service;
                s_backoffMs = 1000;
                s_loggedFirst = false;
                call(service, TX_REGISTER, true);
                s_handler.removeCallbacks(POKE);
                s_handler.post(POKE);
            }

            @Override
            public void onServiceDisconnected(ComponentName name) {
                // Android re-delivers onServiceConnected when the service comes back.
                QGCLogger.w(TAG, "remote-control service disconnected");
                s_binder = null;
                lost();
            }

            @Override
            public void onBindingDied(ComponentName name) {
                s_binder = null;
                lost();
                unbind();
                QGCLogger.w(TAG, "remote-control binding died; rebinding in " + s_backoffMs + " ms");
                s_handler.postDelayed(REBIND, s_backoffMs);
                s_backoffMs = Math.min(s_backoffMs * 2, BACKOFF_MAX_MS);
            }

            @Override
            public void onNullBinding(ComponentName name) {
                QGCLogger.w(TAG, "remote-control service returned a null binding");
            }
        };

        boolean bound = false;
        try {
            bound = s_context.bindService(intent, connection, Context.BIND_AUTO_CREATE);
        } catch (SecurityException e) {
            QGCLogger.w(TAG, "not allowed to bind remote-control service: " + e.getMessage());
        }
        if (bound) {
            s_connection = connection;
            QGCLogger.i(TAG, "binding remote-control service");
        } else {
            // Not this handset, or hidden by a missing <queries> entry. RC_CHANNELS stays in use.
            QGCLogger.i(TAG, "remote-control service unavailable; buttons follow RC_CHANNELS");
        }
    }

    private static void unbind() {
        if (s_connection == null) {
            return;
        }
        try {
            s_context.unbindService(s_connection);
        } catch (IllegalArgumentException ignored) {
            // Already unbound.
        }
        s_connection = null;
    }

    private static void lost() {
        s_live = false;
        try {
            nativeLost();
        } catch (UnsatisfiedLinkError e) {
            QGCLogger.w(TAG, "nativeLost unavailable: " + e);
        }
    }

    private static void call(final IBinder service, final int code, final boolean withCallback) {
        final Parcel data = Parcel.obtain();
        final Parcel reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(CONTROL);
            if (withCallback) {
                data.writeStrongBinder(CALLBACK_BINDER);
            }
            service.transact(code, data, reply, 0);
            reply.readException();
        } catch (RemoteException | RuntimeException e) {
            // A dead service, or a changed transaction layout; the next poke finds out which.
            QGCLogger.w(TAG, "remote-control tx " + code + " failed: " + e.getMessage());
        } finally {
            reply.recycle();
            data.recycle();
        }
    }

    private static int channelNumber(final String name) {
        if (name == null || !name.startsWith(CHANNEL_PREFIX)) {
            return -1;
        }
        try {
            return Integer.parseInt(name.substring(CHANNEL_PREFIX.length()));
        } catch (NumberFormatException e) {
            return -1;
        }
    }

    private static final Binder CALLBACK_BINDER = new Binder() {
        @Override
        protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
            if (code == INTERFACE_TRANSACTION) {
                reply.writeString(CALLBACK);
                return true;
            }
            if (code < FIRST_CALL_TRANSACTION || code > LAST_CALL_TRANSACTION) {
                return super.onTransact(code, data, reply, flags);
            }
            // The service waits on this call and re-throws anything that escapes, so be quick and throw nothing.
            try {
                data.enforceInterface(CALLBACK);
                if (code == CB_CHANNELS) {
                    final int count = data.readInt();
                    final int[] values = new int[CHANNEL_COUNT];
                    Arrays.fill(values, -1);
                    for (int i = 0; i < count; i++) {
                        if (data.readInt() == 0) { // typed-object non-null flag
                            continue;
                        }
                        final int channel = channelNumber(data.readString());
                        final int value = data.readInt();
                        if (channel >= 1 && channel <= CHANNEL_COUNT) {
                            values[channel - 1] = value;
                        }
                    }
                    s_lastAnswer = SystemClock.elapsedRealtime();
                    if (count > 0) {
                        if (!s_loggedFirst) {
                            s_loggedFirst = true;
                            QGCLogger.i(TAG, "first channels: " + Arrays.toString(values));
                        }
                        nativeChannels(values);
                    }
                }
                // Every other callback code is ignored.
            } catch (RuntimeException | UnsatisfiedLinkError e) {
                QGCLogger.w(TAG, "callback " + code + " not handled: " + e.getMessage());
            }
            if (reply != null) {
                reply.writeNoException(); // reply is null for oneway
            }
            return true;
        }
    };
}
