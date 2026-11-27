package com.example.doorlockbt

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.os.Build
import android.os.Bundle
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.Spinner
import android.widget.TextView
import android.widget.Toast
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * 도어락 블루투스 앱 (보드 B 의 HC-06 에 연결)
 *
 * 앱 → 보드 B   OPEN:<비번>  문 열기 (5초 뒤 자동 잠금)
 *               LOCK:<비번>  즉시 잠금
 *               ST?          현재 상태 요청 (연결 직후 자동으로 보냄)
 * 보드 B → 앱   ST:OPEN:<KEY|RFID|BTN|BT>   열림 + 연 방법
 *               ST:LOCK                    잠김
 *               ST:FAIL:<KEY|RFID|BT>      인증 실패 (BT = 앱 비밀번호 틀림)
 */
class MainActivity : Activity() {

    companion object {
        private const val REQ_BT_PERMISSION = 1
        private const val PREFS = "doorlock"
        private const val KEY_ADDRESS = "address"
        private const val KEY_PASSWORD = "password"
        private const val MAX_LOG = 30
    }

    private lateinit var spDevice: Spinner
    private lateinit var btnConnect: Button
    private lateinit var tvConn: TextView
    private lateinit var etPassword: EditText
    private lateinit var tvDoor: TextView
    private lateinit var tvDetail: TextView
    private lateinit var btnOpen: Button
    private lateinit var btnLock: Button
    private lateinit var tvLog: TextView

    private var adapter: BluetoothAdapter? = null
    private var devices: List<BluetoothDevice> = emptyList()
    private val logLines = ArrayDeque<String>()
    private var connecting = false
    private var askedEnable = false      // "블루투스 켜기" 창은 한 번만 띄움 (거절하면 onResume 마다 반복되지 않게)

    private val link = BtLink(
        onConnected = { onConnected() },
        onDisconnected = { reason -> onDisconnected(reason) },
        onLine = { line -> onLine(line) },
    )

    // ================================================================
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        spDevice = findViewById(R.id.spDevice)
        btnConnect = findViewById(R.id.btnConnect)
        tvConn = findViewById(R.id.tvConn)
        etPassword = findViewById(R.id.etPassword)
        tvDoor = findViewById(R.id.tvDoor)
        tvDetail = findViewById(R.id.tvDetail)
        btnOpen = findViewById(R.id.btnOpen)
        btnLock = findViewById(R.id.btnLock)
        tvLog = findViewById(R.id.tvLog)

        val prefs = getSharedPreferences(PREFS, MODE_PRIVATE)
        etPassword.setText(prefs.getString(KEY_PASSWORD, ""))

        adapter = getSystemService(BluetoothManager::class.java)?.adapter
        if (adapter == null) {
            tvConn.text = "이 기기는 블루투스를 지원하지 않아"
            btnConnect.isEnabled = false
            return
        }

        btnConnect.setOnClickListener {
            if (link.isConnected) {
                link.close()
                onDisconnected("연결 끊음")
            } else {
                connectSelected()
            }
        }
        btnOpen.setOnClickListener { sendWithPassword("OPEN") }
        btnLock.setOnClickListener { sendWithPassword("LOCK") }

        if (hasBtPermission()) loadPairedDevices() else requestBtPermission()
    }

    override fun onResume() {
        super.onResume()
        // 설정에서 페어링하고 돌아왔을 수 있으니 목록 새로고침
        if (adapter != null && hasBtPermission() && !link.isConnected && !connecting) loadPairedDevices()
    }

    override fun onDestroy() {
        link.close()
        super.onDestroy()
    }

    // ================================================================
    //  권한 (Android 12 이상은 BLUETOOTH_CONNECT 를 실행 중에 허용받아야 함)
    // ================================================================
    private fun hasBtPermission(): Boolean =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
            checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED

    private fun requestBtPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            requestPermissions(arrayOf(Manifest.permission.BLUETOOTH_CONNECT), REQ_BT_PERMISSION)
        }
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode != REQ_BT_PERMISSION) return
        if (hasBtPermission()) loadPairedDevices()
        else tvConn.text = "블루투스 권한이 없으면 연결할 수 없어 (설정 → 앱 → 권한에서 허용)"
    }

    // ================================================================
    //  페어링된 기기 목록
    // ================================================================
    @SuppressLint("MissingPermission")
    private fun loadPairedDevices() {
        val bt = adapter ?: return
        if (!bt.isEnabled) {
            tvConn.text = "블루투스가 꺼져 있어. 켜고 다시 들어와"
            if (!askedEnable) {
                askedEnable = true
                startActivity(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
            }
            return
        }

        devices = bt.bondedDevices.sortedBy { it.name ?: "" }
        val labels = devices.map { "${it.name ?: "(이름 없음)"}  ${it.address}" }
        spDevice.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, labels)

        if (devices.isEmpty()) {
            tvConn.text = "페어링된 기기가 없어. 휴대폰 설정 → 블루투스에서 HC-06 을 먼저 페어링해 (PIN 기본 1234)"
            return
        }
        // 지난번에 연결한 기기 → 이름에 HC 가 들어간 기기 순서로 자동 선택
        val saved = getSharedPreferences(PREFS, MODE_PRIVATE).getString(KEY_ADDRESS, null)
        var idx = devices.indexOfFirst { it.address == saved }
        if (idx < 0) idx = devices.indexOfFirst { (it.name ?: "").contains("HC", ignoreCase = true) }
        if (idx >= 0) spDevice.setSelection(idx)
        if (!link.isConnected) tvConn.text = "연결 안 됨"
    }

    // ================================================================
    //  연결
    // ================================================================
    private fun connectSelected() {
        if (!hasBtPermission()) { requestBtPermission(); return }
        val device = devices.getOrNull(spDevice.selectedItemPosition) ?: run {
            toast("기기를 먼저 골라")
            return
        }
        getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString(KEY_ADDRESS, device.address).apply()

        connecting = true
        btnConnect.isEnabled = false
        tvConn.text = "연결 중..."
        link.connect(device)
    }

    private fun onConnected() {
        connecting = false
        btnConnect.isEnabled = true
        btnConnect.text = "연결 끊기"
        tvConn.text = "연결됨"
        tvConn.setTextColor(Color.rgb(0x2E, 0x7D, 0x32))
        setControls(true)
        addLog("연결됨")
        link.send("ST?")                        // 현재 문 상태 요청
    }

    private fun onDisconnected(reason: String) {
        connecting = false
        btnConnect.isEnabled = true
        btnConnect.text = "연결"
        tvConn.text = reason
        tvConn.setTextColor(Color.rgb(0xC6, 0x28, 0x28))
        setControls(false)
        showDoor(null, "")
        addLog(reason)
    }

    private fun setControls(enabled: Boolean) {
        btnOpen.isEnabled = enabled
        btnLock.isEnabled = enabled
    }

    // ================================================================
    //  보내기 : OPEN:<비번> / LOCK:<비번>
    // ================================================================
    private fun sendWithPassword(cmd: String) {
        val pw = etPassword.text.toString()
        if (pw.isEmpty()) { toast("비밀번호를 입력해"); return }
        getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString(KEY_PASSWORD, pw).apply()
        link.send("$cmd:$pw")
        addLog("> $cmd:${"*".repeat(pw.length)}")   // 기록에는 비밀번호를 가려서 남김
    }

    // ================================================================
    //  받기 : ST:OPEN:<by> / ST:LOCK / ST:FAIL:<by>
    // ================================================================
    private fun onLine(line: String) {
        addLog("< $line")
        if (!line.startsWith("ST:")) return
        val parts = line.removePrefix("ST:").split(":")
        val by = parts.getOrElse(1) { "" }
        when (parts[0]) {
            "OPEN" -> showDoor(true, if (by.isEmpty()) "" else "${methodName(by)}(으)로 열림 · 5초 뒤 자동 잠금")
            "LOCK" -> showDoor(false, "")
            "FAIL" -> {
                val msg = if (by == "BT") "앱 비밀번호가 틀렸어" else "${methodName(by)} 인증 실패"
                tvDetail.text = msg
                toast(msg)
            }
        }
    }

    private fun methodName(by: String): String = when (by) {
        "KEY" -> "키패드"
        "RFID" -> "RFID 카드"
        "BTN" -> "실내 버튼"
        "BT" -> "앱"
        else -> by
    }

    /** open : true = 열림, false = 잠김, null = 모름(연결 안 됨) */
    private fun showDoor(open: Boolean?, detail: String) {
        when (open) {
            true -> { tvDoor.text = "🔓 열림"; tvDoor.setTextColor(Color.rgb(0x2E, 0x7D, 0x32)) }
            false -> { tvDoor.text = "🔒 잠김"; tvDoor.setTextColor(Color.rgb(0xC6, 0x28, 0x28)) }
            null -> { tvDoor.text = "상태 모름"; tvDoor.setTextColor(Color.GRAY) }
        }
        tvDetail.text = detail
    }

    // ================================================================
    private fun addLog(msg: String) {
        val time = SimpleDateFormat("HH:mm:ss", Locale.KOREA).format(Date())
        logLines.addFirst("$time  $msg")
        while (logLines.size > MAX_LOG) logLines.removeLast()
        tvLog.text = logLines.joinToString("\n")
    }

    private fun toast(msg: String) = Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()
}
