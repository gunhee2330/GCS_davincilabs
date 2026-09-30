package org.mavlink.qgroundcontrol.voicerelay;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

/** Starts the relay when the handset boots and again after this app is updated. */
public final class BootReceiver extends BroadcastReceiver {
    private static final String TAG = "VoiceRelay";

    @Override
    public void onReceive(final Context context, final Intent intent) {
        final String action = intent.getAction();
        if (!Intent.ACTION_BOOT_COMPLETED.equals(action) && !Intent.ACTION_MY_PACKAGE_REPLACED.equals(action)) {
            return;
        }
        try {
            RelayService.start(context);
        } catch (RuntimeException e) {
            // Refused foreground start: the relay waits for the app to be opened.
            Log.w(TAG, "could not start relay on " + action + ": " + e);
        }
    }
}
