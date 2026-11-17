package ai.parsergui.studio

import android.net.Uri
import android.os.Bundle
import android.webkit.JavascriptInterface
import android.webkit.WebView
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import java.io.File

class MainActivity : ComponentActivity() {

    private lateinit var web: WebView
    private var pickedPath: String = ""

    private val picker = registerForActivityResult(ActivityResultContracts.GetContent()) { uri: Uri? ->
        if (uri == null) return@registerForActivityResult
        try {
            val target = File(cacheDir, "capture.png")
            contentResolver.openInputStream(uri)?.use { input ->
                target.outputStream().use { output -> input.copyTo(output) }
            }
            pickedPath = target.absolutePath
        } catch (error: Exception) {
            pickedPath = ""
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        web = WebView(this)
        web.settings.javaScriptEnabled = true
        web.settings.allowFileAccess = true
        web.settings.allowContentAccess = true
        web.settings.domStorageEnabled = true
        web.setBackgroundColor(0xFF0D1015.toInt())
        web.addJavascriptInterface(Bridge(), "Pgr")
        setContentView(web)
        web.loadUrl("file:///android_asset/index.html")
    }

    private fun outputDir(): String {
        val dir = File(filesDir, "out")
        if (!dir.exists()) dir.mkdirs()
        return dir.absolutePath
    }

    inner class Bridge {
        @JavascriptInterface
        fun version(): String = Native.version()

        @JavascriptInterface
        fun outDir(): String = outputDir()

        @JavascriptInterface
        fun pick() {
            runOnUiThread { picker.launch("image/*") }
        }

        @JavascriptInterface
        fun takePicked(): String {
            val path = pickedPath
            pickedPath = ""
            return path
        }

        @JavascriptInterface
        fun parse(path: String, colors: Int): String =
            Native.parse(path, outputDir(), colors)

        @JavascriptInterface
        fun readText(path: String): String = try {
            File(path).readText()
        } catch (error: Exception) {
            ""
        }
    }
}
