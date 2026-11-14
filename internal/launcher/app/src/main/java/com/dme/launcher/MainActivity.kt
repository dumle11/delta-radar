package com.dme.launcher

import android.app.Activity
import android.content.Intent
import android.content.res.Configuration
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.provider.Settings as AndroidSettings
import android.view.View
import android.widget.ArrayAdapter
import android.widget.AutoCompleteTextView
import android.widget.CheckBox
import android.widget.EditText
import android.widget.Filter
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import java.io.File
import com.dme.launcher.overlay.EntityFeedService
import com.dme.launcher.overlay.OverlayService
import kotlin.concurrent.thread

class MainActivity : AppCompatActivity() {

    private val handler = Handler(Looper.getMainLooper())
    private var processes = listOf<ProcessInfo>()
    private var selectedProcess: ProcessInfo? = null
    private var injecting = false
    private var scannerRunning = false

    private val logLines = mutableListOf<String>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        findViewById<TextView>(R.id.btnRefreshProcesses).setOnClickListener { refreshProcesses() }
        findViewById<TextView>(R.id.btnPickLib).setOnClickListener { pickLibrary() }
        findViewById<TextView>(R.id.btnInject).setOnClickListener { doInject() }
        findViewById<TextView>(R.id.btnClearLog).setOnClickListener { clearLog() }
        findViewById<TextView>(R.id.btnStop).setOnClickListener { stopScanner() }

        val autoProcess = findViewById<AutoCompleteTextView>(R.id.autoProcess)
        autoProcess.setOnItemClickListener { _, _, pos, _ ->
            val adapter = autoProcess.adapter
            if (adapter is ProcessAdapter && pos < adapter.count) {
                val item = adapter.getItem(pos)
                selectedProcess = item
                autoProcess.setText(item?.toString() ?: "", false)
            }
        }
        autoProcess.setOnClickListener { autoProcess.showDropDown() }

        requestStoragePermission()

        thread(name = "setup") {
            val hasRoot = RootShell.hasRoot()
            val deployed = if (hasRoot) Injector.isDeployed() else false

            val selinux = if (hasRoot) Injector.getSELinuxStatus() else "unknown"

            handler.post {
                updateRootStatus(hasRoot)
                updateInjectorStatus(deployed)
                updateSELinuxStatus(selinux)

                if (hasRoot && !deployed) {
                    log("Deploying injector binary...")
                    thread {
                        val ok = Injector.deploy(this@MainActivity)
                        handler.post {
                            updateInjectorStatus(ok)
                            log(if (ok) "Injector deployed" else "Failed to deploy injector — place 'andkittyinjector' in assets/")
                        }
                    }
                }
                if (hasRoot) refreshProcesses()
            }
        }
    }

    private fun requestOverlayPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && !Settings.canDrawOverlays(this)) {
            val intent = Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                Uri.parse("package:$packageName"))
            startActivity(intent)
            toast("Grant overlay permission, then return here")
        }
    }

    private fun startOverlay() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && !Settings.canDrawOverlays(this)) {
            requestOverlayPermission()
            return
        }
        EntityFeedService.start(this)
        OverlayService.show(this)
        log("Overlay started — switch to the game")
    }

    private fun stopOverlay() {
        OverlayService.stop(this)
        EntityFeedService.stop(this)
    }

    private fun requestStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                try {
                    startActivity(Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:$packageName")))
                } catch (_: Exception) {
                    startActivity(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION))
                }
            }
        } else {
            requestPermissions(arrayOf(android.Manifest.permission.READ_EXTERNAL_STORAGE), 100)
        }
    }

    private fun updateRootStatus(hasRoot: Boolean) {
        findViewById<TextView>(R.id.txtRoot).apply {
            text = "Root: ${if (hasRoot) "granted" else "not available"}"
            setTextColor(if (hasRoot) 0xFF4fd08a.toInt() else 0xFFe5534b.toInt())
        }
    }

    private fun updateInjectorStatus(deployed: Boolean) {
        findViewById<TextView>(R.id.txtInjector).apply {
            text = "Injector: ${if (deployed) "ready" else "not deployed"}"
            setTextColor(if (deployed) 0xFF4fd08a.toInt() else 0xFF5c6b78.toInt())
        }
    }

    private fun updateSELinuxStatus(status: String) {
        findViewById<TextView>(R.id.txtSELinux).apply {
            val enforcing = status.equals("Enforcing", ignoreCase = true)
            text = "SELinux: $status${if (enforcing) " ⚠ ptrace blocked" else ""}"
            setTextColor(if (enforcing) 0xFFe5534b.toInt() else 0xFF4fd08a.toInt())
        }
    }

    private fun refreshProcesses() {
        log("Scanning processes...")
        thread {
            val list = ProcessLister.listGameProcesses()
            handler.post {
                processes = list
                val auto = findViewById<AutoCompleteTextView>(R.id.autoProcess)
                val adapter = ProcessAdapter(this, list)
                auto.setAdapter(adapter)
                log("Found ${list.size} processes")

                val dfm = list.find { it.name.contains("proxima.dfm", ignoreCase = true) }
                if (dfm != null) {
                    selectedProcess = dfm
                    auto.setText(dfm.toString(), false)
                    log("Auto-selected: ${dfm.name}")
                }
            }
        }
    }

    private fun pickLibrary() {
        val intent = Intent(Intent.ACTION_GET_CONTENT).apply {
            type = "*/*"
            addCategory(Intent.CATEGORY_OPENABLE)
        }
        startActivityForResult(Intent.createChooser(intent, "Select .so library"), REQUEST_PICK_LIB)
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQUEST_PICK_LIB && resultCode == Activity.RESULT_OK) {
            val uri = data?.data ?: return
            copyUriToLocal(uri)
        }
    }

    private fun copyUriToLocal(uri: Uri) {
        thread {
            try {
                val filename = uri.lastPathSegment?.substringAfterLast('/') ?: "library.so"
                val safeName = filename.replace(Regex("[^a-zA-Z0-9._-]"), "_")
                val local = File(noBackupFilesDir, safeName)
                contentResolver.openInputStream(uri)?.use { input ->
                    local.outputStream().use { output -> input.copyTo(output) }
                }

                val remotePath = Injector.pushLibrary(local.absolutePath)
                local.delete()

                handler.post {
                    if (remotePath != null) {
                        findViewById<EditText>(R.id.editLibPath).setText(remotePath)
                        log("Library pushed to $remotePath")
                    } else {
                        log("Failed to push library to device")
                    }
                }
            } catch (e: Exception) {
                handler.post { log("Error: ${e.message}") }
            }
        }
    }

    private fun isProcessAlive(pid: Int): Boolean {
        val result = RootShell.exec("kill -0 $pid 2>/dev/null && echo ALIVE || echo DEAD")
        return result.contains("ALIVE")
    }

    private fun doInject() {
        if (injecting) return
        val libPath = findViewById<EditText>(R.id.editLibPath).text.toString().trim()

        if (libPath.isEmpty()) {
            toast("Enter a library path first"); return
        }

        val autoText = findViewById<AutoCompleteTextView>(R.id.autoProcess).text.toString().trim()
        val process = selectedProcess
            ?: processes.find { it.toString() == autoText }
            ?: processes.find { autoText.contains(it.name, ignoreCase = true) || it.name.contains(autoText, ignoreCase = true) }

        val watchMode = findViewById<CheckBox>(R.id.chkWatch).isChecked

        if (process == null && !watchMode) {
            toast("Select a target process"); return
        }

        val useMemfd = findViewById<CheckBox>(R.id.chkMemfd).isChecked
        val hideLib = findViewById<CheckBox>(R.id.chkHide).isChecked

        injecting = true
        val btn = findViewById<TextView>(R.id.btnInject)
        btn.text = "Injecting..."
        btn.isEnabled = false

        log("--- Injection started ---")

        thread(name = "bg") {
            if (process != null && !watchMode) {
                if (!isProcessAlive(process.pid)) {
                    handler.post {
                        injecting = false
                        btn.text = "Inject"
                        btn.isEnabled = true
                        log("[!] Process ${process.name} (PID ${process.pid}) is DEAD")
                        log("[!] Android killed it in the background")
                        log("[*] TIP: Enable 'Watch mode' and press Inject,")
                        log("[*] then switch to the game. It will inject")
                        log("[*] when the game restarts.")
                    }
                    return@thread
                }
                log("[+] Process ${process.name} (PID ${process.pid}) is alive")
            }

            if (watchMode) {
                handler.post {
                    btn.text = "Watching..."
                    log("[*] Waiting for ${process?.name ?: "target"} to start...")
                    log("[*] Switch to the game now!")
                }
            }

            val success = Injector.inject(
                pid = process?.pid,
                packageName = process?.name,
                libPath = libPath,
                useMemfd = useMemfd,
                hideLib = hideLib,
                watchMode = watchMode,
                onOutput = { line -> handler.post { log(line) } }
            )

            val seAfter = Injector.getSELinuxStatus()
            handler.post { updateSELinuxStatus(seAfter) }

            handler.post {
                injecting = false
                btn.text = "Inject"
                btn.isEnabled = true
                if (success) {
                    scannerRunning = true
                    findViewById<TextView>(R.id.btnStop).visibility = View.VISIBLE
                    startOverlay()
                }
                log(if (success) "Injection completed successfully" else "Injection may have failed — check log output")
            }
        }
    }

    private fun log(message: String) {
        logLines.add(message)
        if (logLines.size > 200) logLines.removeAt(0)
        val txt = findViewById<TextView>(R.id.txtLog)
        txt.text = logLines.joinToString("\n")
        val scroll = findViewById<ScrollView>(R.id.scrollLog)
        scroll.post { scroll.fullScroll(ScrollView.FOCUS_DOWN) }
    }

    private fun stopScanner() {
        log("Sending stop signal...")
        thread {
            val result = RootShell.exec("touch /data/local/tmp/.s7xk && echo STOPPED")
            handler.post {
                if (result.contains("STOPPED")) {
                    scannerRunning = false
                    findViewById<TextView>(R.id.btnStop).visibility = View.GONE
                    stopOverlay()
                    log("Stop signal sent — shutting down shortly")
                } else {
                    log("Failed to send stop signal")
                }
            }
        }
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
    }

    private fun clearLog() {
        logLines.clear()
        findViewById<TextView>(R.id.txtLog).text = ""
    }

    private fun toast(msg: String) = Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()

    companion object {
        private const val REQUEST_PICK_LIB = 1001
    }

    private class ProcessAdapter(
        context: android.content.Context,
        private val allItems: List<ProcessInfo>
    ) : ArrayAdapter<ProcessInfo>(context, android.R.layout.simple_dropdown_item_1line, allItems.toMutableList()) {

        private var filtered: List<ProcessInfo> = allItems

        override fun getCount(): Int = filtered.size
        override fun getItem(position: Int): ProcessInfo? = filtered.getOrNull(position)

        override fun getFilter(): Filter = object : Filter() {
            override fun performFiltering(constraint: CharSequence?): FilterResults {
                val query = constraint?.toString()?.lowercase() ?: ""
                val matches = if (query.isEmpty()) allItems
                    else allItems.filter { it.toString().lowercase().contains(query) }
                return FilterResults().apply { values = matches; count = matches.size }
            }

            @Suppress("UNCHECKED_CAST")
            override fun publishResults(constraint: CharSequence?, results: FilterResults?) {
                filtered = (results?.values as? List<ProcessInfo>) ?: allItems
                if (filtered.isNotEmpty()) notifyDataSetChanged() else notifyDataSetInvalidated()
            }
        }
    }
}
