import java.io.ByteArrayOutputStream

plugins {
    id("com.android.application")
}

// The grumpyscreen checkout this Android project lives in.
val grumpyRoot: File = rootDir.parentFile

fun git(vararg args: String): String? = try {
    val out = ByteArrayOutputStream()
    val proc = ProcessBuilder("git", *args)
        .directory(grumpyRoot)
        .redirectErrorStream(true)
        .start()
    proc.inputStream.copyTo(out)
    if (proc.waitFor() == 0) out.toString().trim().ifEmpty { null } else null
} catch (e: Exception) {
    null
}

// Same values build.sh hands the Makefile; CI can set them explicitly.
val grumpyVersion: String = System.getenv("GUPPYSCREEN_VERSION") ?: git("rev-parse", "HEAD") ?: "unknown"
val grumpyBranch: String = System.getenv("GUPPYSCREEN_BRANCH") ?: git("rev-parse", "--abbrev-ref", "HEAD") ?: "unknown"
val grumpyVersionCode: Int = git("rev-list", "--count", "HEAD")?.toIntOrNull() ?: 1

// The bundled grumpyscreen.cfg is the printer one with its [commands]
// emptied: they are shell commands for the printer host, and the app's tools
// (Settings > Tools) go through Moonraker instead.
val generatedAssets = layout.buildDirectory.dir("generated/grumpyAssets")
val androidConfig by tasks.registering {
    val src = grumpyRoot.resolve("grumpyscreen.cfg")
    // the font LVGL's built-in fonts are generated from, for the app's dialogs
    val font = grumpyRoot.resolve("lvgl/scripts/built_in_font/Montserrat-Medium.ttf")
    inputs.file(src)
    inputs.file(font)
    outputs.dir(generatedAssets)
    doLast {
        var inCommands = false
        val lines = src.readLines().map { line ->
            val t = line.trim()
            if (t.startsWith("[")) inCommands = t == "[commands]"
            if (inCommands && !t.startsWith("[") && !t.startsWith("#") && t.contains(":")) {
                line.substringBefore(":").trim() + ":"
            } else {
                line
            }
        }
        val out = generatedAssets.get().asFile
        out.mkdirs()
        out.resolve("grumpyscreen.cfg").writeText(lines.joinToString("\n") + "\n")
        font.copyTo(out.resolve("Montserrat-Medium.ttf"), overwrite = true)
    }
}

android {
    namespace = "io.github.pellcorp.grumpyscreen"
    compileSdk = 35
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "io.github.pellcorp.grumpyscreen"
        // java.util.function and Map.getOrDefault need API 24
        minSdk = 24
        targetSdk = 35
        versionCode = grumpyVersionCode
        versionName = grumpyVersion.take(12)

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DGRUMPY_ROOT=${grumpyRoot.absolutePath}",
                    "-DGUPPYSCREEN_VERSION=$grumpyVersion",
                    "-DGUPPYSCREEN_BRANCH=$grumpyBranch",
                    "-DANDROID_STL=c++_static",
                )
                (project.findProperty("grumpySmallScreen") as String?)?.let {
                    if (it.toBoolean()) arguments += "-DGUPPY_SMALL_SCREEN=ON"
                }
            }
        }
        ndk {
            // ./gradlew -PgrumpyAbis=arm64-v8a for a quicker single-ABI build
            val abis = (project.findProperty("grumpyAbis") as String?)
                ?: "arm64-v8a,armeabi-v7a,x86_64"
            abiFilters += abis.split(",").map { it.trim() }.filter { it.isNotEmpty() }
        }
    }

    // Release signing comes from the environment (CI secrets). Without a
    // keystore the release APK is signed with the debug key so it still
    // installs.
    val keystore = System.getenv("KEYSTORE")
    signingConfigs {
        if (keystore != null) {
            create("release") {
                storeFile = file(keystore)
                storePassword = System.getenv("KEYSTORE_PASSWORD")
                keyAlias = System.getenv("KEY_ALIAS")
                keyPassword = System.getenv("KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        getByName("release") {
            signingConfig = if (keystore != null) signingConfigs.getByName("release")
                            else signingConfigs.getByName("debug")
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    sourceSets {
        getByName("main") {
            assets.srcDir(generatedAssets)
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = false
        }
    }
}

tasks.named("preBuild") { dependsOn(androidConfig) }
