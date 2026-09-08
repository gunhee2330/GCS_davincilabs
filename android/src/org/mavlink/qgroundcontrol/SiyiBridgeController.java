package org.mavlink.qgroundcontrol;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;

/**
 * Turns on the SIYI controller's UDP<->serial bridge for the police loudspeaker.
 *
 * On the UniRC 7 Pro the loudspeaker payload is reached over the SIYI datalink: the ground
 * station sends to UDP 192.168.144.20:19856 and a background service, com.siyi.udpservice,
 * forwards those bytes to the air unit's serial link (and back). That service starts on
 * boot on its own and remembers whether its serial side is open, so in practice the bridge
 * is already up. This exists so the delivered unit does not depend on someone having left
 * UniGCS's SDK/UDP mode enabled: on our own launch we bind the service and ask it to open,
 * which is idempotent and harmless if it already is.
 *
 * The service exposes an AIDL binder (com.siyi.udpservice.ISerialAidlInterface). We hold no
 * .aidl for it, so we drive transaction 1 by hand: enforceInterface(descriptor) + writeInt(1)
 * opens the serial side. This was read off the shipped service's binder and is only meant
 * for this hardware; it no-ops cleanly anywhere the service is absent.
 *
 * Everything here is best effort. A missing or changed service must never take QGC down, so
 * the whole path is guarded and only logged.
 */
public final class SiyiBridgeController {
    private static final String TAG = "SiyiBridge";
    private static final String PACKAGE = "com.siyi.udpservice";
    private static final String SERVICE = "com.siyi.udpservice.UdpService";
    private static final String DESCRIPTOR = "com.siyi.udpservice.ISerialAidlInterface";
    private static final int TRANSACTION_SET_SERIAL_OPEN = 1; // arg: int, nonzero = open

    private static ServiceConnection s_connection;

    private SiyiBridgeController() {}

    /** Bind the bridge service and ask it to open its serial side. Safe to call once at startup. */
    public static synchronized void initialize(final Context context) {
        if (s_connection != null) {
            return;
        }

        final Intent intent = new Intent();
        intent.setClassName(PACKAGE, SERVICE);

        final ServiceConnection connection = new ServiceConnection() {
            @Override
            public void onServiceConnected(ComponentName name, IBinder service) {
                openSerial(service);
                // Keep the binding: it holds the service up and re-fires onServiceConnected
                // if it is ever restarted, reopening the bridge without any further calls.
            }

            @Override
            public void onServiceDisconnected(ComponentName name) {
                QGCLogger.w(TAG, "SIYI bridge service disconnected");
            }
        };

        boolean bound = false;
        try {
            bound = context.getApplicationContext().bindService(
                intent, connection, Context.BIND_AUTO_CREATE);
        } catch (SecurityException e) {
            QGCLogger.w(TAG, "not allowed to bind SIYI bridge: " + e.getMessage());
        }

        if (bound) {
            s_connection = connection;
            QGCLogger.d(TAG, "binding SIYI bridge service");
        } else {
            // Not this hardware, or the service is gone. The loudspeaker simply falls back
            // to whatever route is already configured; nothing else in QGC is affected.
            QGCLogger.d(TAG, "SIYI bridge service unavailable; skipping");
        }
    }

    private static void openSerial(final IBinder service) {
        final Parcel data = Parcel.obtain();
        final Parcel reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(DESCRIPTOR);
            data.writeInt(1); // open
            service.transact(TRANSACTION_SET_SERIAL_OPEN, data, reply, 0);
            reply.readException();
            QGCLogger.d(TAG, "SIYI serial bridge opened");
        } catch (RemoteException | RuntimeException e) {
            // A different service answering on this name, or a changed transaction layout.
            QGCLogger.w(TAG, "could not open SIYI serial bridge: " + e.getMessage());
        } finally {
            reply.recycle();
            data.recycle();
        }
    }
}
