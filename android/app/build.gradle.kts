import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Release signing. One keystore signs every Alien Trails app: Android only
// requires that an update to a given applicationId carry the same key as the
// version already installed, so sharing one key across separate apps is both
// allowed and normal.
//
// The debug keystore will not do for anything you hand to someone. Gradle
// generates one per machine, so a build from CI and a build from a laptop are
// signed by different keys and cannot upgrade each other -- installing the
// second over the first fails with a signature mismatch until the app is
// uninstalled.
//
// The key is never in the tree. CI decodes it from a repository or (so that
// every app shares one) an organization secret; see
// .github/workflows/release.yml. To sign locally, put an
// android/keystore.properties beside this module:
//
//   storeFile=../alientrails-release.jks
//   storePassword=...
//   keyAlias=alientrails
//
// With a PKCS12 keystore the key password is the store password, so
// keyPassword is optional. With none of it configured the release build is
// simply unsigned, exactly as it was before: a fresh clone still builds.
val keystoreProperties = Properties().apply {
    val file = rootProject.file("keystore.properties")
    if (file.exists()) {
        file.inputStream().use { load(it) }
    }
}

fun keystoreSetting(property: String, environment: String): String? =
    (keystoreProperties.getProperty(property) ?: System.getenv(environment))
        ?.takeIf { it.isNotBlank() }

// Named ks* rather than store*/key*: inside create("release") those are the
// signing config's own properties, and a local of the same name would shadow
// the thing being assigned to.
val ksFile = keystoreSetting("storeFile", "KEYSTORE_FILE")
    ?.let { rootProject.file(it) }
    ?.takeIf { it.isFile }
val ksPassword = keystoreSetting("storePassword", "KEYSTORE_PASSWORD")
val ksAlias = keystoreSetting("keyAlias", "KEY_ALIAS")
val ksKeyPassword = keystoreSetting("keyPassword", "KEY_PASSWORD") ?: ksPassword
val canSignRelease = ksFile != null && ksPassword != null && ksAlias != null

android {
    namespace = "com.helmet.ledcontrol"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.helmet.ledcontrol"
        minSdk = 26
        targetSdk = 34
        versionCode = 5
        versionName = "1.4"
    }

    signingConfigs {
        if (canSignRelease) {
            create("release") {
                storeFile = ksFile
                storePassword = ksPassword
                keyAlias = ksAlias
                keyPassword = ksKeyPassword
                // Spelled out rather than left to the plugin's defaults. v1
                // is JAR signing, only read below API 24, and minSdk is 26.
                // v3 is what carries a rotation lineage, so enabling it now
                // is what keeps replacing this key possible later.
                enableV1Signing = false
                enableV2Signing = true
                enableV3Signing = true
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // Null when no key is configured, which leaves the APK unsigned
            // rather than failing a build that was never going to be shipped.
            signingConfig = signingConfigs.findByName("release")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    buildFeatures {
        compose = true
    }

    composeOptions {
        kotlinCompilerExtensionVersion = "1.5.14"
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.activity:activity-compose:1.9.2")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.4")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.4")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.8.1")

    implementation(platform("androidx.compose:compose-bom:2024.06.00"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material3:material3")
    debugImplementation("androidx.compose.ui:ui-tooling")

    testImplementation("junit:junit:4.13.2")
}
