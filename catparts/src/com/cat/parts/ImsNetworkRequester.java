package com.cat.parts;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.util.Log;

public class ImsNetworkRequester {
    private static final String TAG = "CatParts_IMS";
    private static ConnectivityManager.NetworkCallback sCallback = null;

    public static synchronized void updateState(Context ctx, boolean enable) {
        try {
            ConnectivityManager cm = (ConnectivityManager) ctx.getSystemService(Context.CONNECTIVITY_SERVICE);
            if (cm == null) return;

            if (enable) {
                if (sCallback == null) {
                    NetworkRequest request = new NetworkRequest.Builder()
                            .addCapability(NetworkCapabilities.NET_CAPABILITY_IMS)
                            .addTransportType(NetworkCapabilities.TRANSPORT_CELLULAR)
                            .build();

                    sCallback = new ConnectivityManager.NetworkCallback() {
                        @Override
                        public void onAvailable(Network network) {
                            Log.i(TAG, "IMS network available: " + network);
                        }

                        @Override
                        public void onCapabilitiesChanged(Network network, NetworkCapabilities caps) {
                            Log.i(TAG, "IMS network capabilities: " + caps);
                        }

                        @Override
                        public void onLost(Network network) {
                            Log.i(TAG, "IMS network lost: " + network);
                        }
                    };

                    try {
                        cm.requestNetwork(request, sCallback);
                        Log.i(TAG, "Requested persistent IMS cellular network request via requestNetwork");
                    } catch (SecurityException se) {
                        Log.w(TAG, "requestNetwork restricted, falling back to registerNetworkCallback: " + se.getMessage());
                        cm.registerNetworkCallback(request, sCallback);
                    }
                }
            } else {
                if (sCallback != null) {
                    cm.unregisterNetworkCallback(sCallback);
                    sCallback = null;
                    Log.i(TAG, "Unregistered IMS cellular network request");
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "Failed to update IMS network request: " + e.getMessage());
        }
    }
}
