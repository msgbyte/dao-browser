import groovy.json.JsonSlurper
import org.gradle.api.GradleException
import org.jetbrains.kotlin.gradle.dsl.JvmTarget
import javax.imageio.ImageIO

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "com.msgbyte.dao"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.msgbyte.dao"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "0.1.0"
        resValue(
            "string",
            "mozilla_components_version",
            libs.versions.mozilla.components.get(),
        )
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        compose = true
        resValues = true
    }

    androidResources {
        // Built-in WebExtensions use `_locales`; AAPT's default pattern drops
        // all underscore-prefixed directories and makes the extension invalid.
        ignoreAssetsPattern =
            "!.svn:!.git:!.ds_store:!*.scc:.*:!CVS:!thumbs.db:!picasa.ini:!*~"
    }

    lint {
        // The transitive support-base artifact contains an unused notification helper.
        disable += "NotificationPermission"
    }

    testOptions {
        unitTests.isIncludeAndroidResources = true
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(JvmTarget.JVM_17)
    }
}

configurations.configureEach {
    exclude(group = "androidx.work", module = "work-runtime")
}

val bundledUBlockOriginDirectory =
    layout.projectDirectory.dir("src/main/assets/extensions/ublock_origin")

val bundledKissTranslatorDirectory =
    layout.projectDirectory.dir("src/main/assets/extensions/kiss_translator")

val legacyLauncherIcons = listOf("mdpi", "hdpi", "xhdpi", "xxhdpi", "xxxhdpi").map {
    layout.projectDirectory.file("src/main/res/mipmap-$it/ic_launcher.png")
}

val verifyLegacyLauncherIcons by tasks.registering {
    inputs.files(legacyLauncherIcons)

    doLast {
        legacyLauncherIcons.forEach { iconFile ->
            val bitmap = ImageIO.read(iconFile.asFile)
                ?: throw GradleException("Cannot decode legacy launcher icon: ${iconFile.asFile}")
            var minX = bitmap.width
            var minY = bitmap.height
            var maxX = -1
            var maxY = -1
            for (y in 0 until bitmap.height) {
                for (x in 0 until bitmap.width) {
                    if ((bitmap.getRGB(x, y) ushr 24) > 8) {
                        minX = minOf(minX, x)
                        minY = minOf(minY, y)
                        maxX = maxOf(maxX, x)
                        maxY = maxOf(maxY, y)
                    }
                }
            }
            if (maxX < minX || maxY < minY) {
                throw GradleException("Legacy launcher icon is empty: ${iconFile.asFile}")
            }
            val visibleWidthFraction = (maxX - minX + 1).toDouble() / bitmap.width
            val visibleHeightFraction = (maxY - minY + 1).toDouble() / bitmap.height
            if (maxOf(visibleWidthFraction, visibleHeightFraction) > 0.65) {
                throw GradleException(
                    "Legacy launcher icon overfills its canvas: ${iconFile.asFile} " +
                        "(${"%.1f".format(visibleWidthFraction * 100)}% x " +
                        "${"%.1f".format(visibleHeightFraction * 100)}%)",
                )
            }
        }
    }
}

val verifyBundledUBlockOrigin by tasks.registering {
    val manifestFile = bundledUBlockOriginDirectory.file("manifest.json")
    val provenanceFile =
        layout.projectDirectory.file("src/main/assets/notices/ublock_origin.txt")
    val requiredFiles = listOf(
        bundledUBlockOriginDirectory.file("background.html"),
        bundledUBlockOriginDirectory.file("js/background.js"),
        bundledUBlockOriginDirectory.file("js/start.js"),
        bundledUBlockOriginDirectory.file("assets/assets.json"),
        bundledUBlockOriginDirectory.file("_locales/en/messages.json"),
        bundledUBlockOriginDirectory.file("LICENSE.txt"),
    )
    inputs.file(manifestFile)
    inputs.file(provenanceFile)
    inputs.files(requiredFiles)

    doLast {
        val manifestPath = manifestFile.asFile
        if (!manifestPath.isFile) {
            throw GradleException("Bundled uBlock Origin manifest is missing")
        }
        val manifest = JsonSlurper().parse(manifestPath) as Map<*, *>
        val browserSettings = manifest["browser_specific_settings"] as? Map<*, *>
        val geckoSettings = browserSettings?.get("gecko") as? Map<*, *>
        if (manifest["version"] != "1.72.2") {
            throw GradleException("Bundled uBlock Origin version must be 1.72.2")
        }
        if ((manifest["manifest_version"] as? Number)?.toInt() != 2) {
            throw GradleException("Bundled uBlock Origin must use manifest version 2")
        }
        if (geckoSettings?.get("id") != "uBlock0@raymondhill.net") {
            throw GradleException("Bundled uBlock Origin extension ID is invalid")
        }
        val permissions = manifest["permissions"] as? List<*> ?: emptyList<Any>()
        if ("menus" in permissions) {
            throw GradleException(
                "Bundled uBlock Origin must not request GeckoView-unsupported menus permission",
            )
        }
        if (manifest.containsKey("commands")) {
            throw GradleException(
                "Bundled uBlock Origin must not declare GeckoView-unsupported commands",
            )
        }
        val provenance = provenanceFile.asFile.readText()
        listOf(
            "uBlock Origin 1.72.2",
            "40c315b0da7871868155ecfae7a50a58dfa0920aebd865e008214986f1b7c578",
            "https://github.com/gorhill/uBlock",
            "GNU General Public License 3.0",
        ).forEach { requiredValue ->
            if (requiredValue !in provenance) {
                throw GradleException(
                    "Bundled uBlock Origin provenance is missing: $requiredValue",
                )
            }
        }
        requiredFiles.forEach { requiredFile ->
            if (!requiredFile.asFile.isFile) {
                throw GradleException(
                    "Bundled uBlock Origin file is missing: ${requiredFile.asFile.name}",
                )
            }
        }
    }
}

val verifyBundledKissTranslator by tasks.registering {
    val manifestFile = bundledKissTranslatorDirectory.file("manifest.json")
    val provenanceFile =
        layout.projectDirectory.file("src/main/assets/notices/kiss_translator.txt")
    val licenseFile =
        layout.projectDirectory.file("src/main/assets/notices/kiss_translator_license.txt")
    val requiredFiles = listOf(
        bundledKissTranslatorDirectory.file("background.js"),
        bundledKissTranslatorDirectory.file("content.js"),
        bundledKissTranslatorDirectory.file("popup.html"),
        bundledKissTranslatorDirectory.file("popup.js"),
        bundledKissTranslatorDirectory.file("options.html"),
        bundledKissTranslatorDirectory.file("_locales/en/messages.json"),
        bundledKissTranslatorDirectory.file("_locales/zh_CN/messages.json"),
        bundledKissTranslatorDirectory.file("META-INF/cose.sig"),
    )
    inputs.file(manifestFile)
    inputs.file(provenanceFile)
    inputs.file(licenseFile)
    inputs.files(requiredFiles)

    doLast {
        val manifestPath = manifestFile.asFile
        if (!manifestPath.isFile) {
            throw GradleException("Bundled KISS Translator manifest is missing")
        }
        val manifest = JsonSlurper().parse(manifestPath) as Map<*, *>
        val browserSettings = manifest["browser_specific_settings"] as? Map<*, *>
        val geckoSettings = browserSettings?.get("gecko") as? Map<*, *>
        if (manifest["version"] != "2.0.29") {
            throw GradleException("Bundled KISS Translator version must be 2.0.29")
        }
        if ((manifest["manifest_version"] as? Number)?.toInt() != 2) {
            throw GradleException("Bundled KISS Translator must use manifest version 2")
        }
        if (geckoSettings?.get("id") != "{fb25c100-22ce-4d5a-be7e-75f3d6f0fc13}") {
            throw GradleException("Bundled KISS Translator extension ID is invalid")
        }
        val provenance = provenanceFile.asFile.readText()
        listOf(
            "KISS Translator 2.0.29",
            "0316f026b1b0c3d171262b3f4f369e34013abdf1d1bbe9d348290c3db53f4092",
            "https://github.com/fishjar/kiss-translator",
            "GNU General Public License v3.0 only",
        ).forEach { requiredValue ->
            if (requiredValue !in provenance) {
                throw GradleException(
                    "Bundled KISS Translator provenance is missing: $requiredValue",
                )
            }
        }
        if (!licenseFile.asFile.isFile) {
            throw GradleException("Bundled KISS Translator license is missing")
        }
        requiredFiles.forEach { requiredFile ->
            if (!requiredFile.asFile.isFile) {
                throw GradleException(
                    "Bundled KISS Translator file is missing: ${requiredFile.asFile.name}",
                )
            }
        }
    }
}

tasks.named("preBuild").configure {
    dependsOn(verifyLegacyLauncherIcons)
    dependsOn(verifyBundledUBlockOrigin)
    dependsOn(verifyBundledKissTranslator)
}

dependencies {
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.datastore.preferences)
    implementation(libs.androidx.swiperefreshlayout)
    implementation(libs.androidx.camera.camera2)
    implementation(libs.androidx.camera.lifecycle)
    implementation(libs.androidx.camera.view)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(platform(libs.compose.bom))
    implementation(libs.compose.material3)
    implementation(libs.compose.ui)
    implementation(libs.compose.ui.tooling.preview)
    implementation(libs.lucide.icons)
    implementation(libs.mozilla.concept.engine)
    implementation(libs.mozilla.browser.engine.gecko)
    implementation(libs.mozilla.browser.state)
    implementation(libs.mozilla.browser.session.storage)
    implementation(libs.zxing.core)

    debugImplementation(libs.compose.ui.tooling)
    debugImplementation(libs.compose.ui.test.manifest)

    testImplementation(libs.junit)
    testImplementation(libs.mockk)
    testImplementation(libs.robolectric)
    testImplementation(libs.androidx.test.core.ktx)
    testImplementation(libs.kotlinx.coroutines.test)

    androidTestImplementation(libs.androidx.test.ext.junit)
    androidTestImplementation(libs.androidx.test.runner)
    androidTestImplementation(libs.espresso.core)
    androidTestImplementation(platform(libs.compose.bom))
    androidTestImplementation(libs.compose.ui.test.junit4)
    androidTestImplementation(libs.mockk.android)
}
