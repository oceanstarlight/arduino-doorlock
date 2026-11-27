package com.example.doorlockbt

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothSocket
import android.os.Handler
import android.os.Looper
import java.io.IOException
import java.util.UUID
import java.util.concurrent.Executors

/**
 * HC-06 과의 클래식 블루투스(SPP) 연결.
 * - 연결·수신은 백그라운드 스레드에서 하고, 결과(콜백)는 메인(UI) 스레드로 넘긴다.
 * - 받은 글자는 '\r' 또는 '\n' 이 올 때마다 한 줄로 묶어 [onLine] 으로 전달한다.
 */
class BtLink(
    private val onConnected: () -> Unit,
    private val onDisconnected: (reason: String) -> Unit,
    private val onLine: (line: String) -> Unit,
) {
    companion object {
        /** SPP(Serial Port Profile) 표준 UUID. HC-05/HC-06 모두 이 값을 쓴다. */
        private val SPP_UUID: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")
    }

    private val main = Handler(Looper.getMainLooper())
    private val writer = Executors.newSingleThreadExecutor()

    @Volatile private var socket: BluetoothSocket? = null

    val isConnected: Boolean get() = socket?.isConnected == true

    @SuppressLint("MissingPermission")   // 권한은 MainActivity 에서 확인한 뒤 호출
    fun connect(device: BluetoothDevice) {
        close()
        Thread {
            val s = try {
                openSocket(device)
            } catch (e: IOException) {
                main.post { onDisconnected("연결 실패: ${e.message}") }
                return@Thread
            }
            socket = s
            main.post(onConnected)
            readLoop(s)
        }.start()
    }

    /** 한 줄 보내기. 보드 B 는 '\r' 이나 '\n' 이 오면 한 줄로 처리한다. */
    fun send(line: String) {
        val s = socket ?: return
        writer.execute {
            try {
                s.outputStream.write("$line\n".toByteArray(Charsets.US_ASCII))
                s.outputStream.flush()
            } catch (e: IOException) {
                closeWith(s, "보내기 실패: ${e.message}")
            }
        }
    }

    @Synchronized
    fun close() {
        val s = socket ?: return
        socket = null
        try { s.close() } catch (_: IOException) {}
    }

    // ---------------------------------------------------------------

    @SuppressLint("MissingPermission")
    private fun openSocket(device: BluetoothDevice): BluetoothSocket {
        // 일반(보안) 연결을 먼저 시도하고, 실패하면 비보안 연결로 한 번 더 시도
        // (일부 HC-06 / 휴대폰 조합은 비보안 연결만 된다)
        val secure = device.createRfcommSocketToServiceRecord(SPP_UUID)
        try {
            secure.connect()
            return secure
        } catch (e: IOException) {
            try { secure.close() } catch (_: IOException) {}
        }
        val insecure = device.createInsecureRfcommSocketToServiceRecord(SPP_UUID)
        try {
            insecure.connect()
            return insecure
        } catch (e: IOException) {
            try { insecure.close() } catch (_: IOException) {}
            throw e
        }
    }

    private fun readLoop(s: BluetoothSocket) {
        val buf = ByteArray(64)
        val line = StringBuilder()
        try {
            val input = s.inputStream
            while (true) {
                val n = input.read(buf)
                if (n < 0) break
                for (i in 0 until n) {
                    val c = buf[i].toInt().toChar()
                    if (c == '\r' || c == '\n') {
                        if (line.isNotEmpty()) {
                            val text = line.toString()
                            line.setLength(0)
                            main.post { onLine(text) }
                        }
                    } else if (line.length < 64) {
                        line.append(c)
                    }
                }
            }
            closeWith(s, "연결이 끊겼어")
        } catch (e: IOException) {
            closeWith(s, "연결이 끊겼어")
        }
    }

    /** s 가 지금 쓰는 소켓일 때만 끊고 알린다 (close() 로 직접 끊었거나 새로 연결한 경우는 무시) */
    @Synchronized
    private fun closeWith(s: BluetoothSocket, reason: String) {
        if (socket !== s) return
        close()
        main.post { onDisconnected(reason) }
    }
}
