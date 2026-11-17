package ai.parsergui.studio

class Native {
    companion object {
        init {
            System.loadLibrary("pgr_core")
        }

        @JvmStatic
        external fun parse(path: String, outDir: String, maxColors: Int): String

        @JvmStatic
        external fun version(): String
    }
}
