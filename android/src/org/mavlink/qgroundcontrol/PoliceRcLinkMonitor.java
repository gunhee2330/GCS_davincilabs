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

/**
 * Reads the UniRC 7 Pro handset's own video-link strength for the police top bar.
 *
 * The flight controller's RC rssi says nothing about the handset's radio, so we ask the SIYI
 * service UniGCS itself reads: biz.siyi.remotecontrol's RemoteControlService, AIDL interface
 * biz.siyi.pilot.rcuservice.IRemoteControl. We hold no .aidl for it; the transactions below
 * were read off UniGCS 3.2.1 (rcuservice/b.java, callback g6/b.java, LinkInfo.writeToParcel):
 *   control tx 1  = register an IRemoteCallback binder
 *   control tx 43 = start the LinkInfo stream (idempotent: cancels and restarts it)
 *   callback 1    = air unit connected (int, edge-triggered)
 *   callback 9    = LinkInfo (non-null flag, 5 ints, then 10 nullable Integers)
 *
 * Best effort throughout. If the service is missing or renamed, the natives never hear
 * anything, PoliceRcLink stays unavailable and the top bar keeps the FC rssi.
 * Nothing calls tx 44 (stop): it is global and would stop UniGCS's stream too.
 */
public final class PoliceRcLinkMonitor {
    private static final String TAG = "PoliceRcLink";
    private static final String PACKAGE = "biz.siyi.remotecontrol";
    private static final String SERVICE = "biz.siyi.pilot.rcuservice.RemoteControlService";
    private static final String CONTROL = "biz.siyi.pilot.rcuservice.IRemoteControl";
    private static final String CALLBACK = "biz.siyi.pilot.rcuservice.callback.IRemoteCallback";
    private static final int TX_REGISTER = 1;
    private static final int TX_LINKINFO_START = 43;
    private static final int CB_CONNECTED = 1;
    private static final int CB_LINKINFO = 9;
    private static final long WATCHDOG_MS = 2000;
    private static final long STALE_MS = 3000;
    private static final long BACKOFF_MAX_MS = 30000;

    private static Context s_context;
    private static ServiceConnection s_connection;
    private static volatile IBinder s_binder;
    private static volatile long s_lastLinkInfo;
    private static long s_backoffMs = 1000;
    private static boolean s_loggedFirst;
    private static final Handler s_handler = new Handler(Looper.getMainLooper());

    /** -1 = null field. */
    static native void nativeLinkInfo(int strength, int quality, int validPercent);
    static native void nativeConnected(boolean connected);

    private PoliceRcLinkMonitor() {}

    /** Bind the SIYI remote-control service. Idempotent; call after the natives are registered. */
    public static synchronized void start(final Context context) {
        if (s_context != null) {
            return;
        }
        s_context = context.getApplicationContext();
        bind();
        s_handler.postDelayed(WATCHDOG, WATCHDOG_MS);
    }

    // ponytail: fixed 2 s poll; switch to a code-1 edge trigger if the extra tx43 turns out noisy.
    private static final Runnable WATCHDOG = new Runnable() {
        @Override
        public void run() {
            final IBinder binder = s_binder;
            if (binder != null && SystemClock.elapsedRealtime() - s_lastLinkInfo > STALE_MS) {
                // UniGCS's tx44 or an RCU not attached yet at our tx43: ask again.
                startStream(binder);
            }
            s_handler.postDelayed(this, WATCHDOG_MS);
        }
    };

    private static final Runnable REBIND = new Runnable() {
        @Override
        public void run() {
            bind();
        }
    };

    private static synchronized void bind() {
        // Never startService: SIYI starts this service at boot.
        final Intent intent = new Intent();
        intent.setClassName(PACKAGE, SERVICE);

        final ServiceConnection connection = new ServiceConnection() {
            @Override
            public void onServiceConnected(ComponentName name, IBinder service) {
                s_binder = service;
                s_backoffMs = 1000;
                s_loggedFirst = false;
                // Receiver-connected (code 1) fires only on change, so an edge missed while
                // unbound would stick; start every binding as connected until told otherwise.
                try {
                    nativeConnected(true);
                } catch (UnsatisfiedLinkError | RuntimeException e) {
                    QGCLogger.w(TAG, "nativeConnected unavailable: " + e);
                }
                register(service);
                startStream(service);
            }

            @Override
            public void onServiceDisconnected(ComponentName name) {
                // Android re-delivers onServiceConnected when the service comes back.
                s_binder = null;
                QGCLogger.w(TAG, "remote-control service disconnected");
            }

            @Override
            public void onBindingDied(ComponentName name) {
                s_binder = null;
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
            QGCLogger.d(TAG, "binding remote-control service");
        } else {
            // Not this handset, or hidden by a missing <queries> entry. FC rssi stays in use.
            QGCLogger.d(TAG, "remote-control service unavailable; skipping");
        }
    }

    private static synchronized void unbind() {
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

    private static void register(final IBinder service) {
        call(service, TX_REGISTER, true);
    }

    private static void startStream(final IBinder service) {
        call(service, TX_LINKINFO_START, false);
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
            // A different service on this name, or a changed transaction layout.
            QGCLogger.w(TAG, "remote-control tx " + code + " failed: " + e.getMessage());
        } finally {
            reply.recycle();
            data.recycle();
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
            // The service re-throws anything that escapes here, so nothing may.
            try {
                data.enforceInterface(CALLBACK);
                if (code == CB_CONNECTED) {
                    nativeConnected(data.readInt() != 0);
                } else if (code == CB_LINKINFO && data.readInt() != 0) { // typed-object non-null flag
                    final int[] d = new int[5];
                    for (int i = 0; i < 5; i++) {
                        d[i] = data.readInt(); // dataFrequency, dataValidPkg, dataPkgLossRate, dataUpload, dataDownload
                    }
                    // imgFrequency, imgValidPkg, imgPkgLossRate, imgUpload, imgDownload,
                    // imgDataMaxLen, validPackagePercent, imgChannel, imgStrength, imgQuality
                    final int[] v = new int[10];
                    for (int i = 0; i < 10; i++) {
                        v[i] = data.readInt() != 0 ? data.readInt() : -1;
                    }
                    s_lastLinkInfo = SystemClock.elapsedRealtime();
                    if (!s_loggedFirst) {
                        s_loggedFirst = true;
                        QGCLogger.i(TAG, "first LinkInfo: data " + java.util.Arrays.toString(d)
                                + ", img " + java.util.Arrays.toString(v));
                    }
                    nativeLinkInfo(v[8], v[9], v[6]);
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
